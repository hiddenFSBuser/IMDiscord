#include "pch.h"
#include "storage.h"
#include "crypto.h"
#include "hwid.h"
#include "log.h"
#include "system/io/ufile.h"
#include "net/json.h"

namespace
{
    // magic | version | nonce(24) | tag(16) | ciphertext
    const unsigned int TOKEN_MAGIC = 0x314D4449; // "IDM1"
    const unsigned int TOKEN_VERSION = 1;

    struct token_header
    {
        unsigned int magic;
        unsigned int version;
        unsigned int payload_len;
        unsigned int reserved;
        unsigned char nonce[24];
        unsigned char tag[16];
    };

    void derive_key(unsigned char out[32])
    {
        unsigned char id[32];
        hwid::get(id);

        crypto::sha256_ctx ctx;
        crypto::sha256_init(&ctx);
        const char* label = "IMDiscord/token-key/v1";
        crypto::sha256_update(&ctx, label, (unsigned int)ccslenf(label));
        crypto::sha256_update(&ctx, id, 32);
        crypto::sha256_final(&ctx, out);
    }

    bool token_path(wchar_t* out, int cap)
    {
        return ufile::app_path(L"auth.bin", out, cap);
    }

    // The seal both the single token and the account list use: the header is
    // authenticated as additional data, so a file edited by hand fails the tag
    // rather than loading half.
    bool seal_to(const wchar_t* path, const void* plain, unsigned int len)
    {
        if (!plain || !len || len > (1u << 20)) return false;

        unsigned char key[32];
        derive_key(key);

        token_header hdr;
        ccfset(&hdr, 0, sizeof(hdr));
        hdr.magic = TOKEN_MAGIC;
        hdr.version = TOKEN_VERSION;
        hdr.payload_len = len;
        crypto::random_bytes(hdr.nonce, 24);

        unsigned char* cipher = (unsigned char*)memalloc((int)len);
        if (!cipher) { ccfset(key, 0, sizeof(key)); return false; }

        crypto::xchacha20poly1305_encrypt(key, hdr.nonce, &hdr.magic, sizeof(unsigned int) * 4,
                                          plain, len, cipher, hdr.tag);

        ubuffer blob;
        blob.init(sizeof(hdr) + len);
        blob.append(&hdr, (unsigned int)sizeof(hdr));
        blob.append(cipher, len);
        ccfset(cipher, 0, len);
        memfree(cipher);

        bool ok = ufile::write_all(path, blob.data, blob.size);
        blob.free_buffer();
        ccfset(key, 0, sizeof(key));
        return ok;
    }

    // Appends a NUL so the result can be read as text. Returns false when the
    // file is missing, malformed, or sealed for a different machine.
    bool unseal_from(const wchar_t* path, ubuffer* out)
    {
        ubuffer blob;
        blob.init();
        if (!ufile::read_all(path, &blob)) { blob.free_buffer(); return false; }

        bool ok = false;
        if (blob.size > sizeof(token_header))
        {
            token_header* hdr = (token_header*)blob.data;
            unsigned int cipher_len = blob.size - (unsigned int)sizeof(token_header);

            if (hdr->magic == TOKEN_MAGIC && hdr->version == TOKEN_VERSION &&
                hdr->payload_len == cipher_len)
            {
                unsigned char key[32];
                derive_key(key);

                unsigned char* plain = (unsigned char*)memalloc((int)cipher_len + 1);
                if (plain)
                {
                    if (crypto::xchacha20poly1305_decrypt(key, hdr->nonce, &hdr->magic,
                                                          sizeof(unsigned int) * 4,
                                                          blob.data + sizeof(token_header),
                                                          cipher_len, hdr->tag, plain))
                    {
                        plain[cipher_len] = 0;
                        out->append(plain, cipher_len + 1);
                        ok = true;
                    }
                    ccfset(plain, 0, cipher_len);
                    memfree(plain);
                }
                ccfset(key, 0, sizeof(key));
            }
        }

        blob.free_buffer();
        return ok;
    }

    // ---- accounts ----
    ulist<saved_account> g_accounts;
    int g_active_account = -1;
    bool g_accounts_loaded = false;
    proxy_config g_default_proxy;

    bool accounts_path(wchar_t* out, int cap)
    {
        return ufile::app_path(L"accounts.bin", out, cap);
    }

    // ---- settings ----
    struct setting_entry
    {
        char key[64];
        char value[512];
    };

    ulist<setting_entry> g_settings;
    bool g_settings_loaded = false;

    setting_entry* find_setting(const char* key)
    {
        for (unsigned int i = 0; i < g_settings.count; i++)
            if (ccscmp(g_settings[i].key, key) == 0) return &g_settings[i];
        return 0;
    }
}

void storage::account_tag(unsigned long long id, char* out, int cap)
{
    if (!out || cap < 3) return;
    out[0] = 0;

    unsigned char machine[32];
    hwid::get(machine);

    crypto::sha256_ctx ctx;
    crypto::sha256_init(&ctx);
    const char* label = "IMDiscord/account-tag/v1";
    crypto::sha256_update(&ctx, label, (unsigned int)ccslenf(label));
    crypto::sha256_update(&ctx, machine, 32);
    crypto::sha256_update(&ctx, &id, sizeof(id));

    unsigned char digest[32];
    crypto::sha256_final(&ctx, digest);

    // Sixteen bytes of it is far more than enough to keep accounts apart.
    const char* hex = "0123456789abcdef";
    int at = 0;
    for (int i = 0; i < 16 && at + 2 < cap; i++)
    {
        out[at++] = hex[digest[i] >> 4];
        out[at++] = hex[digest[i] & 0x0F];
    }
    out[at] = 0;
}

bool storage::save_token(const char* token)
{
    if (!token || !token[0]) return false;

    unsigned int len = (unsigned int)ccslenf(token);
    if (len > 4096) return false;

    unsigned char key[32];
    derive_key(key);

    token_header hdr;
    ccfset(&hdr, 0, sizeof(hdr));
    hdr.magic = TOKEN_MAGIC;
    hdr.version = TOKEN_VERSION;
    hdr.payload_len = len;
    crypto::random_bytes(hdr.nonce, 24);

    unsigned char* cipher = (unsigned char*)memalloc((int)len);
    if (!cipher) return false;

    crypto::xchacha20poly1305_encrypt(key, hdr.nonce, &hdr.magic, sizeof(unsigned int) * 4,
                                      token, len, cipher, hdr.tag);

    ubuffer blob;
    blob.init(sizeof(hdr) + len);
    blob.append(&hdr, (unsigned int)sizeof(hdr));
    blob.append(cipher, len);
    memfree(cipher);

    wchar_t path[MAX_PATH];
    bool ok = token_path(path, MAX_PATH) && ufile::write_all(path, blob.data, blob.size);
    blob.free_buffer();

    ccfset(key, 0, sizeof(key));
    if (ok) log_line("storage: token sealed for this machine");
    else log_line("storage: failed to write the token file");
    return ok;
}

bool storage::load_token(char* out, int cap)
{
    if (!out || cap < 2) return false;
    out[0] = 0;

    wchar_t path[MAX_PATH];
    if (!token_path(path, MAX_PATH)) return false;

    ubuffer blob;
    blob.init();
    if (!ufile::read_all(path, &blob))
    {
        blob.free_buffer();
        return false;
    }

    bool ok = false;
    if (blob.size > sizeof(token_header))
    {
        token_header* hdr = (token_header*)blob.data;
        unsigned int cipher_len = blob.size - (unsigned int)sizeof(token_header);

        if (hdr->magic == TOKEN_MAGIC && hdr->version == TOKEN_VERSION &&
            hdr->payload_len == cipher_len && cipher_len < (unsigned int)cap)
        {
            unsigned char key[32];
            derive_key(key);

            unsigned char* plain = (unsigned char*)memalloc((int)cipher_len + 1);
            if (plain)
            {
                if (crypto::xchacha20poly1305_decrypt(key, hdr->nonce, &hdr->magic,
                                                      sizeof(unsigned int) * 4,
                                                      blob.data + sizeof(token_header), cipher_len,
                                                      hdr->tag, plain))
                {
                    ccpy(out, plain, cipher_len);
                    out[cipher_len] = 0;
                    ok = true;
                }
                else
                {
                    log_line("storage: token does not belong to this machine, discarding");
                }
                ccfset(plain, 0, cipher_len);
                memfree(plain);
            }
            ccfset(key, 0, sizeof(key));
        }
    }

    blob.free_buffer();
    return ok;
}

void storage::clear_token()
{
    wchar_t path[MAX_PATH];
    if (token_path(path, MAX_PATH)) DeleteFileW(path);
}

bool storage::has_token()
{
    wchar_t path[MAX_PATH];
    return token_path(path, MAX_PATH) && ufile::exists(path);
}

// ---------------------------------------------------------------------------
// accounts
// ---------------------------------------------------------------------------

void storage::accounts_load()
{
    if (g_accounts_loaded) return;
    g_accounts_loaded = true;
    g_accounts = ulist<saved_account>();
    g_active_account = -1;

    wchar_t path[MAX_PATH];
    if (!accounts_path(path, MAX_PATH)) return;

    ubuffer plain;
    plain.init();

    if (unseal_from(path, &plain))
    {
        jdoc doc;
        doc.init();
        if (doc.parse((const char*)plain.data, (int)plain.size) && doc.r()->type == JTYPE_OBJ)
        {
            const jval* list = doc.r()->arr("accounts");
            for (unsigned int i = 0; i < list->count; i++)
            {
                const jval* a = list->at(i);
                const char* token = a->str("token", 0);
                if (!token || !token[0]) continue;

                saved_account entry;
                ccfset(&entry, 0, sizeof(entry));
                ccstrncpy(entry.token, token, sizeof(entry.token) - 1);
                ccstrncpy(entry.name, a->str("name", ""), sizeof(entry.name) - 1);
                ccstrncpy(entry.avatar, a->str("avatar", ""), sizeof(entry.avatar) - 1);
                entry.id = a->sf("id");

                entry.is_bot = a->boolean("bot", false);
                entry.proxy_override = a->boolean("proxy_own", false);

                const jval* px = a->obj("proxy");
                if (px && px->type == JTYPE_OBJ)
                {
                    entry.proxy.kind = px->i32("kind", PROXY_NONE);
                    ccstrncpy(entry.proxy.host, px->str("host", ""), sizeof(entry.proxy.host) - 1);
                    entry.proxy.port = (unsigned short)px->i32("port", 0);
                    ccstrncpy(entry.proxy.user, px->str("user", ""), sizeof(entry.proxy.user) - 1);
                    ccstrncpy(entry.proxy.pass, px->str("pass", ""), sizeof(entry.proxy.pass) - 1);
                }

                g_accounts.push(entry);
            }

            g_active_account = (int)doc.r()->i64("active", -1);

            ccfset(&g_default_proxy, 0, sizeof(g_default_proxy));
            const jval* dpx = doc.r()->obj("default_proxy");
            if (dpx && dpx->type == JTYPE_OBJ)
            {
                g_default_proxy.kind = dpx->i32("kind", PROXY_NONE);
                ccstrncpy(g_default_proxy.host, dpx->str("host", ""), sizeof(g_default_proxy.host) - 1);
                g_default_proxy.port = (unsigned short)dpx->i32("port", 0);
                ccstrncpy(g_default_proxy.user, dpx->str("user", ""), sizeof(g_default_proxy.user) - 1);
                ccstrncpy(g_default_proxy.pass, dpx->str("pass", ""), sizeof(g_default_proxy.pass) - 1);
            }
        }
        doc.free_doc();
    }

    plain.free_buffer();

    // Nothing stored yet, but there may be a token from before this list
    // existed. Carrying it over means an upgrade does not sign anybody out.
    if (g_accounts.count == 0)
    {
        char legacy[512];
        if (load_token(legacy, sizeof(legacy)) && legacy[0])
        {
            saved_account entry;
            ccfset(&entry, 0, sizeof(entry));
            ccstrncpy(entry.token, legacy, sizeof(entry.token) - 1);
            ccstrncpy(entry.name, tr("Аккаунт"), sizeof(entry.name) - 1);
            g_accounts.push(entry);
            g_active_account = 0;
            accounts_save();
            log_line("storage: старый токен перенесён в список аккаунтов");
        }
        ccfset(legacy, 0, sizeof(legacy));
    }

    if (g_active_account >= (int)g_accounts.count) g_active_account = -1;
}

void storage::accounts_save()
{
    jwriter w;
    w.init();
    w.begin_obj();
    w.kv_i64("active", g_active_account);

    if (g_default_proxy.kind != PROXY_NONE)
    {
        w.key("default_proxy");
        w.begin_obj();
        w.kv_i64("kind", g_default_proxy.kind);
        w.kv_str("host", g_default_proxy.host);
        w.kv_i64("port", g_default_proxy.port);
        w.kv_str("user", g_default_proxy.user);
        w.kv_str("pass", g_default_proxy.pass);
        w.end_obj();
    }
    w.key("accounts");
    w.begin_arr();
    for (unsigned int i = 0; i < g_accounts.count; i++)
    {
        w.begin_obj();
        w.kv_str("token", g_accounts[i].token);
        w.kv_str("name", g_accounts[i].name);
        w.kv_str("avatar", g_accounts[i].avatar);
        w.kv_snowflake("id", g_accounts[i].id);

        if (g_accounts[i].is_bot) w.kv_bool("bot", true);
        if (g_accounts[i].proxy_override) w.kv_bool("proxy_own", true);

        if (g_accounts[i].proxy.kind != PROXY_NONE)
        {
            w.key("proxy");
            w.begin_obj();
            w.kv_i64("kind", g_accounts[i].proxy.kind);
            w.kv_str("host", g_accounts[i].proxy.host);
            w.kv_i64("port", g_accounts[i].proxy.port);
            w.kv_str("user", g_accounts[i].proxy.user);
            w.kv_str("pass", g_accounts[i].proxy.pass);
            w.end_obj();
        }

        w.end_obj();
    }
    w.end_arr();
    w.end_obj();

    wchar_t path[MAX_PATH];
    if (accounts_path(path, MAX_PATH))
        seal_to(path, w.buf.data, w.buf.size);

    // The buffer held every token in the clear.
    ccfset(w.buf.data, 0, w.buf.size);
    w.free_writer();
}

int storage::accounts_count()
{
    accounts_load();
    return (int)g_accounts.count;
}

const saved_account* storage::account_at(int index)
{
    accounts_load();
    if (index < 0 || index >= (int)g_accounts.count) return 0;
    return &g_accounts[index];
}

int storage::account_remember(const char* token, unsigned long long id,
                              const char* name, const char* avatar, bool is_bot)
{
    accounts_load();
    if (!token || !token[0]) return -1;

    // The id is what identifies an account; the token is only how we got in,
    // and it changes whenever the password does. Matching on it as well means
    // re-adding the same account with a fresh token updates the entry instead
    // of leaving two that look identical.
    int found = -1;
    for (unsigned int i = 0; i < g_accounts.count && found < 0; i++)
    {
        if (id && g_accounts[i].id == id) found = (int)i;
        else if (ccscmp(g_accounts[i].token, token) == 0) found = (int)i;
    }

    if (found < 0)
    {
        saved_account fresh;
        ccfset(&fresh, 0, sizeof(fresh));
        g_accounts.push(fresh);
        found = (int)g_accounts.count - 1;
    }

    saved_account* entry = &g_accounts[found];
    ccfset(entry->token, 0, sizeof(entry->token));
    ccstrncpy(entry->token, token, sizeof(entry->token) - 1);
    entry->is_bot = is_bot;
    if (id) entry->id = id;
    if (name && name[0])
    {
        ccfset(entry->name, 0, sizeof(entry->name));
        ccstrncpy(entry->name, name, sizeof(entry->name) - 1);
    }
    ccfset(entry->avatar, 0, sizeof(entry->avatar));
    if (avatar) ccstrncpy(entry->avatar, avatar, sizeof(entry->avatar) - 1);

    accounts_save();
    return found;
}

void storage::account_forget(int index)
{
    accounts_load();
    if (index < 0 || index >= (int)g_accounts.count) return;

    ccfset(&g_accounts[index], 0, sizeof(saved_account));
    g_accounts.delete_at((unsigned int)index);

    if (g_active_account == index) g_active_account = -1;
    else if (g_active_account > index) g_active_account--;

    accounts_save();
}

int storage::active_account()
{
    accounts_load();
    return g_active_account;
}

void storage::set_active_account(int index)
{
    accounts_load();
    if (index < -1 || index >= (int)g_accounts.count) return;
    g_active_account = index;
    accounts_save();
}

// ---------------------------------------------------------------------------
// settings
// ---------------------------------------------------------------------------

void storage::settings_load()
{
    if (g_settings_loaded) return;
    g_settings_loaded = true;
    g_settings = ulist<setting_entry>();

    wchar_t path[MAX_PATH];
    if (!ufile::app_path(L"settings.json", path, MAX_PATH)) return;

    ubuffer blob;
    blob.init();
    if (!ufile::read_all(path, &blob))
    {
        blob.free_buffer();
        return;
    }

    jdoc doc;
    doc.init();
    if (doc.parse((const char*)blob.c_str(), (int)blob.size) && doc.root->type == JTYPE_OBJ)
    {
        for (unsigned int i = 0; i < doc.root->count; i++)
        {
            const jmember* m = doc.root->member_at(i);
            if (!m || m->value->type != JTYPE_STR) continue;

            setting_entry e;
            ccfset(&e, 0, sizeof(e));
            ccstrncpy(e.key, m->key, sizeof(e.key) - 1);
            ccstrncpy(e.value, m->value->sval, sizeof(e.value) - 1);
            g_settings.push(e);
        }
    }
    doc.free_doc();
    blob.free_buffer();
}

void storage::settings_save()
{
    jwriter w;
    w.init();
    w.begin_obj();
    for (unsigned int i = 0; i < g_settings.count; i++)
        w.kv_str(g_settings[i].key, g_settings[i].value);
    w.end_obj();

    wchar_t path[MAX_PATH];
    if (ufile::app_path(L"settings.json", path, MAX_PATH))
        ufile::write_all(path, w.buf.data, w.buf.size);

    w.free_writer();
}

const char* storage::settings_get(const char* key, const char* def)
{
    settings_load();
    setting_entry* e = find_setting(key);
    return e ? e->value : def;
}

int storage::settings_get_int(const char* key, int def)
{
    settings_load();
    setting_entry* e = find_setting(key);
    return e ? ccstrti(e->value) : def;
}

void storage::settings_set(const char* key, const char* value)
{
    settings_load();

    setting_entry* e = find_setting(key);
    if (!e)
    {
        setting_entry fresh;
        ccfset(&fresh, 0, sizeof(fresh));
        ccstrncpy(fresh.key, key, sizeof(fresh.key) - 1);
        g_settings.push(fresh);
        e = &g_settings[g_settings.count - 1];
    }
    ccfset(e->value, 0, sizeof(e->value));
    ccstrncpy(e->value, value, sizeof(e->value) - 1);
}

void storage::settings_set_int(const char* key, int value)
{
    char tmp[32];
    cnprint(tmp, sizeof(tmp), "%d", value);
    settings_set(key, tmp);
}

int storage::settings_count()
{
    settings_load();
    return (int)g_settings.count;
}

const char* storage::settings_key_at(int index)
{
    settings_load();
    if (index < 0 || index >= (int)g_settings.count) return "";
    return g_settings[index].key;
}

const char* storage::settings_value_at(int index)
{
    settings_load();
    if (index < 0 || index >= (int)g_settings.count) return "";
    return g_settings[index].value;
}

void storage::account_set_proxy(int index, const proxy_config* cfg, bool override_default)
{
    accounts_load();
    if (index < 0 || index >= (int)g_accounts.count || !cfg) return;

    g_accounts[index].proxy = *cfg;
    g_accounts[index].proxy_override = override_default;
    accounts_save();

    log_line("storage: прокси аккаунта %d - %s", index,
             !override_default ? "общий" : (cfg->in_use() ? cfg->host : "напрямую"));
}

bool storage::account_overrides_proxy(int index)
{
    accounts_load();
    if (index < 0 || index >= (int)g_accounts.count) return false;
    return g_accounts[index].proxy_override;
}

proxy_config storage::default_proxy()
{
    accounts_load();
    return g_default_proxy;
}

void storage::set_default_proxy(const proxy_config* cfg)
{
    accounts_load();
    if (!cfg) return;

    g_default_proxy = *cfg;
    accounts_save();

    log_line("storage: общий прокси - %s", cfg->in_use() ? cfg->host : "нет");
}

proxy_config storage::account_proxy(unsigned long long id)
{
    accounts_load();

    proxy_config none;
    ccfset(&none, 0, sizeof(none));

    (void)none;
    if (!id) return g_default_proxy;

    for (unsigned int i = 0; i < g_accounts.count; i++)
        if (g_accounts[i].id == id)
            return g_accounts[i].proxy_override ? g_accounts[i].proxy : g_default_proxy;

    return g_default_proxy;
}

proxy_config storage::active_proxy()
{
    accounts_load();

    proxy_config none;
    ccfset(&none, 0, sizeof(none));

    (void)none;
    if (g_active_account < 0 || g_active_account >= (int)g_accounts.count)
        return g_default_proxy;

    const saved_account* a = &g_accounts[g_active_account];
    return a->proxy_override ? a->proxy : g_default_proxy;
}
