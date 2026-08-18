#include "pch.h"
#include "privacy.h"
#include "rest.h"

#include "core/log.h"
#include "core/crypto.h"
#include "net/json.h"
#include "net/http.h"

// The protobuf here is handled at the wire level rather than through a schema.
// Only two of its fields need naming; everything else is copied through
// untouched, which is the whole point - see privacy.h.

namespace
{
    // PreloadedUserSettings.privacy
    const unsigned int FIELD_PRIVACY = 8;

    // PrivacySettings, as seen in a capture of the real client.
    const unsigned int FIELD_RESTRICTED_GUILDS = 3;   // packed fixed64 ids
    const unsigned int FIELD_DEFAULT_RESTRICTED = 4;  // varint bool

    ubuffer g_blob;            // the whole settings proto, as fetched
    bool g_have = false;
    volatile long g_busy = 0;

    // ---- reading ---------------------------------------------------------

    bool read_varint(const unsigned char* p, unsigned int len, unsigned int* at,
                     unsigned long long* out)
    {
        unsigned long long v = 0;
        int shift = 0;

        while (*at < len)
        {
            unsigned char x = p[(*at)++];
            v |= (unsigned long long)(x & 0x7F) << shift;
            if (!(x & 0x80)) { *out = v; return true; }
            shift += 7;
            if (shift > 63) return false;
        }
        return false;
    }

    // Points at one field's payload inside a message, or returns false.
    bool find_field(const unsigned char* msg, unsigned int len, unsigned int want,
                    unsigned int* out_start, unsigned int* out_len,
                    unsigned int* out_tag_start)
    {
        unsigned int at = 0;

        while (at < len)
        {
            unsigned int tag_start = at;

            unsigned long long tag = 0;
            if (!read_varint(msg, len, &at, &tag)) return false;

            unsigned int field = (unsigned int)(tag >> 3);
            unsigned int wire = (unsigned int)(tag & 7);

            if (wire == 2)
            {
                unsigned long long n = 0;
                if (!read_varint(msg, len, &at, &n)) return false;
                if (at + n > len) return false;

                if (field == want)
                {
                    *out_start = at;
                    *out_len = (unsigned int)n;
                    *out_tag_start = tag_start;
                    return true;
                }
                at += (unsigned int)n;
            }
            else if (wire == 0)
            {
                unsigned long long v = 0;
                unsigned int value_start = at;
                if (!read_varint(msg, len, &at, &v)) return false;

                if (field == want)
                {
                    *out_start = value_start;
                    *out_len = at - value_start;
                    *out_tag_start = tag_start;
                    return true;
                }
            }
            else if (wire == 5) { at += 4; }
            else if (wire == 1) { at += 8; }
            else return false;
        }
        return false;
    }

    // ---- writing ---------------------------------------------------------

    void put_varint(ubuffer* out, unsigned long long v)
    {
        unsigned char b[10];
        int n = 0;

        do {
            b[n] = (unsigned char)(v & 0x7F);
            v >>= 7;
            if (v) b[n] |= 0x80;
            n++;
        } while (v);

        out->append(b, (unsigned int)n);
    }

    void put_tag(ubuffer* out, unsigned int field, unsigned int wire)
    {
        put_varint(out, ((unsigned long long)field << 3) | wire);
    }

    // Rebuilds a message with one field replaced, every other byte kept as it
    // was found. Passing no replacement removes the field.
    void rewrite_field(const unsigned char* msg, unsigned int len, unsigned int want,
                       const unsigned char* body, unsigned int body_len, unsigned int wire,
                       ubuffer* out)
    {
        unsigned int at = 0;
        bool written = false;

        while (at < len)
        {
            unsigned int tag_start = at;

            unsigned long long tag = 0;
            if (!read_varint(msg, len, &at, &tag)) break;

            unsigned int field = (unsigned int)(tag >> 3);
            unsigned int w = (unsigned int)(tag & 7);
            unsigned int end = at;

            if (w == 2)
            {
                unsigned long long n = 0;
                if (!read_varint(msg, len, &end, &n)) break;
                end += (unsigned int)n;
            }
            else if (w == 0)
            {
                unsigned long long v = 0;
                if (!read_varint(msg, len, &end, &v)) break;
            }
            else if (w == 5) end += 4;
            else if (w == 1) end += 8;
            else break;

            if (end > len) break;

            if (field == want)
            {
                // Replaced in place, so its position among the others is kept.
                if (body_len || wire == 0)
                {
                    put_tag(out, want, wire);
                    if (wire == 2) put_varint(out, body_len);
                    out->append(body, body_len);
                }
                written = true;
            }
            else
            {
                out->append(msg + tag_start, end - tag_start);
            }

            at = end;
        }

        if (!written && (body_len || wire == 0))
        {
            put_tag(out, want, wire);
            if (wire == 2) put_varint(out, body_len);
            out->append(body, body_len);
        }
    }

    // ---- the job ---------------------------------------------------------

    struct patch_job
    {
        unsigned int field;
        unsigned int wire;
        unsigned int body_len;
        unsigned char body[512];
    };

    void job_fetch(void*)
    {
        http_response res;
        res.init();

        if (api::call("GET", "/users/@me/settings-proto/1", 0, &res) && res.ok())
        {
            jdoc doc;
            doc.init();

            if (doc.parse(res.text(), (int)res.body.size))
            {
                const char* b64 = doc.root->str("settings", 0);
                if (b64 && b64[0])
                {
                    g_blob.clear();
                    if (crypto::base64_decode(b64, (int)ccslenf(b64), &g_blob)) g_have = true;
                }
            }
            doc.free_doc();
        }
        else
        {
            api::set_last_error(tr("Настройки приватности не загрузились"));
        }

        res.free_response();
        InterlockedExchange(&g_busy, 0);
    }

    void job_patch(void* user)
    {
        patch_job* j = (patch_job*)user;

        // Only the privacy subtree goes out. Discord merges what it is given,
        // and sending the whole settings blob back would return every other
        // section to whatever this client happened to have fetched.
        ubuffer privacy;
        privacy.init(256);

        unsigned int start = 0, len = 0, tag = 0;
        if (g_have && find_field(g_blob.data, g_blob.size, FIELD_PRIVACY, &start, &len, &tag))
            rewrite_field(g_blob.data + start, len, j->field, j->body, j->body_len, j->wire, &privacy);
        else
            rewrite_field(0, 0, j->field, j->body, j->body_len, j->wire, &privacy);

        ubuffer message;
        message.init(privacy.size + 16);
        put_tag(&message, FIELD_PRIVACY, 2);
        put_varint(&message, privacy.size);
        message.append(privacy.data, privacy.size);

        ubuffer b64;
        b64.init(message.size * 2 + 8);
        crypto::base64_encode(message.data, message.size, &b64);
        b64.c_str();

        jwriter w;
        w.init();
        w.begin_obj();
        w.kv_str("settings", (const char*)b64.data);
        w.end_obj();

        http_response res;
        res.init();

        if (!api::call("PATCH", "/users/@me/settings-proto/1", w.buf.c_str(), &res) || !res.ok())
            api::set_last_error(tr("Настройки приватности не сохранились"));

        res.free_response();
        w.free_writer();
        b64.free_buffer();
        message.free_buffer();
        privacy.free_buffer();
        memfree(j);

        // What the server actually stored, rather than what was asked for.
        privacy::fetch();
    }

    void send_patch(unsigned int field, unsigned int wire,
                    const unsigned char* body, unsigned int body_len)
    {
        if (body_len > 512) return;

        patch_job* j = (patch_job*)memalloc(sizeof(patch_job));
        if (!j) return;

        ccfset(j, 0, sizeof(*j));
        j->field = field;
        j->wire = wire;
        j->body_len = body_len;
        if (body_len) ccpy(j->body, body, body_len);

        jobs::post(job_patch, j);
    }

    // The restricted list as it stands, as ids.
    int read_restricted(snowflake* out, int cap)
    {
        if (!g_have) return 0;

        unsigned int start = 0, len = 0, tag = 0;
        if (!find_field(g_blob.data, g_blob.size, FIELD_PRIVACY, &start, &len, &tag)) return 0;

        unsigned int s2 = 0, l2 = 0, t2 = 0;
        if (!find_field(g_blob.data + start, len, FIELD_RESTRICTED_GUILDS, &s2, &l2, &t2))
            return 0;

        // Packed fixed64: eight little endian bytes each, no tags between.
        const unsigned char* p = g_blob.data + start + s2;
        int count = 0;

        for (unsigned int i = 0; i + 8 <= l2 && count < cap; i += 8)
        {
            snowflake id = 0;
            for (int b = 7; b >= 0; b--) id = (id << 8) | p[i + b];
            out[count++] = id;
        }
        return count;
    }
}

void privacy::fetch()
{
    if (InterlockedCompareExchange(&g_busy, 1, 0) != 0) return;
    if (!g_blob.data) g_blob.init(4096);
    jobs::post(job_fetch, 0);
}

bool privacy::ready() { return g_have; }
bool privacy::busy() { return g_busy != 0; }

bool privacy::dms_allowed_from(snowflake guild_id)
{
    snowflake blocked[128];
    int count = read_restricted(blocked, 128);

    for (int i = 0; i < count; i++)
        if (blocked[i] == guild_id) return false;

    return true;
}

void privacy::set_dms_allowed_from(snowflake guild_id, bool allowed)
{
    snowflake blocked[128];
    int count = read_restricted(blocked, 128);

    unsigned char body[128 * 8];
    unsigned int len = 0;

    for (int i = 0; i < count; i++)
    {
        if (blocked[i] == guild_id) continue;      // dropped if it is going
        for (int b = 0; b < 8; b++) body[len++] = (unsigned char)(blocked[i] >> (b * 8));
    }

    if (!allowed && len + 8 <= sizeof(body))
        for (int b = 0; b < 8; b++) body[len++] = (unsigned char)(guild_id >> (b * 8));

    send_patch(FIELD_RESTRICTED_GUILDS, 2, body, len);
}

bool privacy::dms_allowed_by_default()
{
    if (!g_have) return true;

    unsigned int start = 0, len = 0, tag = 0;
    if (!find_field(g_blob.data, g_blob.size, FIELD_PRIVACY, &start, &len, &tag)) return true;

    unsigned int s2 = 0, l2 = 0, t2 = 0;
    if (!find_field(g_blob.data + start, len, FIELD_DEFAULT_RESTRICTED, &s2, &l2, &t2))
        return true;

    unsigned int at = 0;
    unsigned long long v = 0;
    read_varint(g_blob.data + start + s2, l2, &at, &v);

    // Stored as the restriction, read here as the permission.
    return v == 0;
}

void privacy::set_dms_allowed_by_default(bool allowed)
{
    unsigned char body[2];
    body[0] = allowed ? 0 : 1;
    send_patch(FIELD_DEFAULT_RESTRICTED, 0, body, 1);
}
