#include "pch.h"
#include "json.h"

// ---------------------------------------------------------------------------
// value accessors
// ---------------------------------------------------------------------------

static jval g_null_value = { JTYPE_NULL, false, 0, 0.0, 0, { 0 } };

const jval* jdoc::null_value()
{
    return &g_null_value;
}

static bool key_equals(const jmember* m, const char* key)
{
    unsigned int i = 0;
    for (; i < m->key_len; i++)
    {
        if (key[i] == 0 || key[i] != m->key[i]) return false;
    }
    return key[i] == 0;
}

const jval* jval::get(const char* key) const
{
    if (type != JTYPE_OBJ || !key) return &g_null_value;
    for (unsigned int i = 0; i < count; i++)
    {
        if (key_equals(&members[i], key)) return members[i].value;
    }
    return &g_null_value;
}

bool jval::has(const char* key) const
{
    if (type != JTYPE_OBJ || !key) return false;
    for (unsigned int i = 0; i < count; i++)
        if (key_equals(&members[i], key)) return true;
    return false;
}

const jval* jval::at(unsigned int index) const
{
    if (type == JTYPE_ARR && index < count) return &items[index];
    if (type == JTYPE_OBJ && index < count) return members[index].value;
    return &g_null_value;
}

const jmember* jval::member_at(unsigned int index) const
{
    if (type == JTYPE_OBJ && index < count) return &members[index];
    return 0;
}

const char* jval::as_str(const char* def) const
{
    return type == JTYPE_STR ? sval : def;
}

long long jval::as_i64(long long def) const
{
    if (type == JTYPE_NUM) return inum;
    if (type == JTYPE_STR) return ccstrtoll(sval, 0, 10);
    if (type == JTYPE_BOOL) return bval ? 1 : 0;
    return def;
}

unsigned long long jval::as_u64(unsigned long long def) const
{
    if (type == JTYPE_NUM) return (unsigned long long)inum;
    if (type == JTYPE_STR) return ccstrtoull(sval, 0, 10);
    if (type == JTYPE_BOOL) return bval ? 1 : 0;
    return def;
}

double jval::as_dbl(double def) const
{
    if (type == JTYPE_NUM) return num;
    if (type == JTYPE_STR) return (double)ccstrtf(sval);
    return def;
}

bool jval::as_bool(bool def) const
{
    if (type == JTYPE_BOOL) return bval;
    if (type == JTYPE_NUM) return inum != 0;
    return def;
}

unsigned long long jval::as_snowflake() const
{
    if (type == JTYPE_STR) return ccstrtoull(sval, 0, 10);
    if (type == JTYPE_NUM) return (unsigned long long)inum;
    return 0;
}

const char* jval::str(const char* key, const char* def) const { return get(key)->as_str(def); }
long long jval::i64(const char* key, long long def) const { return get(key)->as_i64(def); }
unsigned long long jval::u64(const char* key, unsigned long long def) const { return get(key)->as_u64(def); }
int jval::i32(const char* key, int def) const { return (int)get(key)->as_i64(def); }
double jval::dbl(const char* key, double def) const { return get(key)->as_dbl(def); }
bool jval::boolean(const char* key, bool def) const { return get(key)->as_bool(def); }
unsigned long long jval::sf(const char* key) const { return get(key)->as_snowflake(); }

const jval* jval::arr(const char* key) const
{
    const jval* v = get(key);
    return v->type == JTYPE_ARR ? v : &g_null_value;
}

const jval* jval::obj(const char* key) const
{
    const jval* v = get(key);
    return v->type == JTYPE_OBJ ? v : &g_null_value;
}

// ---------------------------------------------------------------------------
// parser
// ---------------------------------------------------------------------------

namespace
{
    const int MAX_DEPTH = 64;

    struct jparser
    {
        const char* p;
        const char* end;
        uarena* ar;
        ulist<jval> vstack;      // staging area for array elements
        ulist<jmember> mstack;   // staging area for object members
        ubuffer scratch;         // unescaping buffer
        int depth;

        void skip_ws()
        {
            while (p < end)
            {
                char c = *p;
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') p++;
                else break;
            }
        }

        static void emit_utf8(ubuffer* out, unsigned int cp)
        {
            if (cp < 0x80)
            {
                out->append_char((char)cp);
            }
            else if (cp < 0x800)
            {
                out->append_char((char)(0xC0 | (cp >> 6)));
                out->append_char((char)(0x80 | (cp & 0x3F)));
            }
            else if (cp < 0x10000)
            {
                out->append_char((char)(0xE0 | (cp >> 12)));
                out->append_char((char)(0x80 | ((cp >> 6) & 0x3F)));
                out->append_char((char)(0x80 | (cp & 0x3F)));
            }
            else
            {
                out->append_char((char)(0xF0 | (cp >> 18)));
                out->append_char((char)(0x80 | ((cp >> 12) & 0x3F)));
                out->append_char((char)(0x80 | ((cp >> 6) & 0x3F)));
                out->append_char((char)(0x80 | (cp & 0x3F)));
            }
        }

        static int hex4(const char* s)
        {
            int v = 0;
            for (int i = 0; i < 4; i++)
            {
                char c = s[i];
                int d;
                if (c >= '0' && c <= '9') d = c - '0';
                else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
                else return -1;
                v = (v << 4) | d;
            }
            return v;
        }

        // Returns an arena copy of the string starting at the opening quote.
        bool parse_string(const char** out, unsigned int* out_len)
        {
            if (p >= end || *p != '\"') return false;
            p++;

            const char* start = p;
            bool escaped = false;
            while (p < end && *p != '\"')
            {
                if (*p == '\\')
                {
                    escaped = true;
                    p++;
                    if (p >= end) return false;
                }
                p++;
            }
            if (p >= end) return false;

            const char* stop = p;
            p++; // closing quote

            if (!escaped)
            {
                unsigned int len = (unsigned int)(stop - start);
                *out = ar->dup(start, (int)len);
                *out_len = len;
                return *out != 0;
            }

            scratch.clear();
            const char* s = start;
            while (s < stop)
            {
                if (*s != '\\')
                {
                    scratch.append_char(*s++);
                    continue;
                }
                s++;
                if (s >= stop) break;
                switch (*s)
                {
                case '\"': scratch.append_char('\"'); s++; break;
                case '\\': scratch.append_char('\\'); s++; break;
                case '/':  scratch.append_char('/');  s++; break;
                case 'b':  scratch.append_char('\b'); s++; break;
                case 'f':  scratch.append_char('\f'); s++; break;
                case 'n':  scratch.append_char('\n'); s++; break;
                case 'r':  scratch.append_char('\r'); s++; break;
                case 't':  scratch.append_char('\t'); s++; break;
                case 'u':
                {
                    if (s + 5 > stop) { s = stop; break; }
                    int cp = hex4(s + 1);
                    s += 5;
                    if (cp < 0) break;
                    if (cp >= 0xD800 && cp <= 0xDBFF && s + 6 <= stop && s[0] == '\\' && s[1] == 'u')
                    {
                        int lo = hex4(s + 2);
                        if (lo >= 0xDC00 && lo <= 0xDFFF)
                        {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            s += 6;
                        }
                    }
                    emit_utf8(&scratch, (unsigned int)cp);
                    break;
                }
                default:
                    scratch.append_char(*s++);
                    break;
                }
            }

            *out = ar->dup((const char*)scratch.data, (int)scratch.size);
            *out_len = scratch.size;
            return *out != 0;
        }

        bool parse_number(jval* out)
        {
            const char* start = p;
            bool neg = false;
            if (p < end && (*p == '-' || *p == '+'))
            {
                neg = (*p == '-');
                p++;
            }

            unsigned long long mantissa = 0;
            int digits = 0;
            while (p < end && *p >= '0' && *p <= '9')
            {
                if (digits < 19) { mantissa = mantissa * 10 + (unsigned)(*p - '0'); digits++; }
                else { /* overflow guard: exponent handles the rest */ }
                p++;
            }

            bool is_int = true;
            int frac_digits = 0;
            if (p < end && *p == '.')
            {
                is_int = false;
                p++;
                while (p < end && *p >= '0' && *p <= '9')
                {
                    if (digits + frac_digits < 19) { mantissa = mantissa * 10 + (unsigned)(*p - '0'); frac_digits++; }
                    p++;
                }
            }

            int exponent = -frac_digits;
            if (p < end && (*p == 'e' || *p == 'E'))
            {
                is_int = false;
                p++;
                bool eneg = false;
                if (p < end && (*p == '-' || *p == '+')) { eneg = (*p == '-'); p++; }
                int ev = 0;
                while (p < end && *p >= '0' && *p <= '9') { ev = ev * 10 + (*p - '0'); p++; }
                exponent += eneg ? -ev : ev;
            }

            if (p == start) return false;

            double value = (double)mantissa;
            if (exponent > 0)
            {
                for (int i = 0; i < exponent && i < 308; i++) value *= 10.0;
            }
            else if (exponent < 0)
            {
                for (int i = 0; i < -exponent && i < 308; i++) value /= 10.0;
            }
            if (neg) value = -value;

            out->type = JTYPE_NUM;
            out->num = value;
            out->inum = is_int ? (neg ? -(long long)mantissa : (long long)mantissa) : (long long)value;
            return true;
        }

        bool parse_value(jval* out)
        {
            if (depth > MAX_DEPTH) return false;
            skip_ws();
            if (p >= end) return false;

            char c = *p;
            switch (c)
            {
            case '{':
            {
                p++;
                depth++;
                unsigned int base = mstack.count;
                skip_ws();
                if (p < end && *p == '}') { p++; }
                else
                {
                    for (;;)
                    {
                        skip_ws();
                        jmember m;
                        m.key = 0;
                        m.key_len = 0;
                        m.value = 0;
                        if (!parse_string(&m.key, &m.key_len)) { depth--; return false; }
                        skip_ws();
                        if (p >= end || *p != ':') { depth--; return false; }
                        p++;

                        jval tmp;
                        ccfset(&tmp, 0, sizeof(tmp));
                        if (!parse_value(&tmp)) { depth--; return false; }

                        jval* stored = (jval*)ar->alloc(sizeof(jval), 8);
                        if (!stored) { depth--; return false; }
                        *stored = tmp;
                        m.value = stored;
                        mstack.push(m);

                        skip_ws();
                        if (p < end && *p == ',') { p++; continue; }
                        if (p < end && *p == '}') { p++; break; }
                        depth--;
                        return false;
                    }
                }
                depth--;

                unsigned int n = mstack.count - base;
                out->type = JTYPE_OBJ;
                out->count = n;
                if (n)
                {
                    jmember* dst = (jmember*)ar->alloc(n * (unsigned int)sizeof(jmember), 8);
                    if (!dst) return false;
                    ccpy(dst, (char*)mstack.listPTR + base * sizeof(jmember), n * sizeof(jmember));
                    out->members = dst;
                }
                else
                {
                    out->members = 0;
                }
                mstack.count = base;
                return true;
            }

            case '[':
            {
                p++;
                depth++;
                unsigned int base = vstack.count;
                skip_ws();
                if (p < end && *p == ']') { p++; }
                else
                {
                    for (;;)
                    {
                        jval tmp;
                        ccfset(&tmp, 0, sizeof(tmp));
                        if (!parse_value(&tmp)) { depth--; return false; }
                        vstack.push(tmp);

                        skip_ws();
                        if (p < end && *p == ',') { p++; continue; }
                        if (p < end && *p == ']') { p++; break; }
                        depth--;
                        return false;
                    }
                }
                depth--;

                unsigned int n = vstack.count - base;
                out->type = JTYPE_ARR;
                out->count = n;
                if (n)
                {
                    jval* dst = (jval*)ar->alloc(n * (unsigned int)sizeof(jval), 8);
                    if (!dst) return false;
                    ccpy(dst, (char*)vstack.listPTR + base * sizeof(jval), n * sizeof(jval));
                    out->items = dst;
                }
                else
                {
                    out->items = 0;
                }
                vstack.count = base;
                return true;
            }

            case '\"':
            {
                const char* s = 0;
                unsigned int len = 0;
                if (!parse_string(&s, &len)) return false;
                out->type = JTYPE_STR;
                out->sval = s;
                out->count = len;
                return true;
            }

            case 't':
                if (end - p < 4 || p[1] != 'r' || p[2] != 'u' || p[3] != 'e') return false;
                p += 4;
                out->type = JTYPE_BOOL;
                out->bval = true;
                return true;

            case 'f':
                if (end - p < 5 || p[1] != 'a' || p[2] != 'l' || p[3] != 's' || p[4] != 'e') return false;
                p += 5;
                out->type = JTYPE_BOOL;
                out->bval = false;
                return true;

            case 'n':
                if (end - p < 4 || p[1] != 'u' || p[2] != 'l' || p[3] != 'l') return false;
                p += 4;
                out->type = JTYPE_NULL;
                return true;

            default:
                if (c == '-' || c == '+' || (c >= '0' && c <= '9')) return parse_number(out);
                return false;
            }
        }
    };
}

void jdoc::init()
{
    arena.init();
    root = 0;
}

bool jdoc::parse(const char* text, int len)
{
    arena.reset();
    root = 0;
    if (!text) return false;
    if (len < 0) len = (int)ccslenf(text);

    jparser ps;
    ps.p = text;
    ps.end = text + len;
    ps.ar = &arena;
    ps.depth = 0;
    ps.vstack = ulist<jval>();
    ps.mstack = ulist<jmember>();
    ps.scratch.init();

    jval tmp;
    ccfset(&tmp, 0, sizeof(tmp));
    bool ok = ps.parse_value(&tmp);

    ps.vstack.dispose();
    ps.mstack.dispose();
    ps.scratch.free_buffer();

    if (!ok)
    {
        arena.reset();
        return false;
    }

    root = (jval*)arena.alloc(sizeof(jval), 8);
    if (!root) return false;
    *root = tmp;
    return true;
}

void jdoc::free_doc()
{
    arena.reset();
    root = 0;
}

// ---------------------------------------------------------------------------
// writer
// ---------------------------------------------------------------------------

void jwriter::init()
{
    buf.init(1024);
    depth = 0;
    ccfset(need_comma, 0, sizeof(need_comma));
}

void jwriter::free_writer()
{
    buf.free_buffer();
}

void jwriter::prefix()
{
    if (depth < 32 && need_comma[depth]) buf.append_char(',');
    if (depth < 32) need_comma[depth] = true;
}

void jwriter::begin_obj()
{
    prefix();
    buf.append_char('{');
    if (depth < 31) { depth++; need_comma[depth] = false; }
}

void jwriter::end_obj()
{
    if (depth > 0) depth--;
    buf.append_char('}');
}

void jwriter::begin_arr()
{
    prefix();
    buf.append_char('[');
    if (depth < 31) { depth++; need_comma[depth] = false; }
}

void jwriter::end_arr()
{
    if (depth > 0) depth--;
    buf.append_char(']');
}

void jwriter::key(const char* name)
{
    prefix();
    buf.append_char('\"');
    buf.append_json_escaped(name);
    buf.append_char('\"');
    buf.append_char(':');
    // The value that follows belongs to this key, not to a new list slot.
    if (depth < 32) need_comma[depth] = false;
}

void jwriter::val_str(const char* v, int len)
{
    if (!v) { val_null(); return; }
    prefix();
    buf.append_char('\"');
    buf.append_json_escaped(v, len);
    buf.append_char('\"');
}

void jwriter::val_i64(long long v)
{
    prefix();
    buf.append_fmt("%lld", v);
}

void jwriter::val_u64(unsigned long long v)
{
    prefix();
    buf.append_fmt("%llu", v);
}

void jwriter::val_dbl(double v)
{
    prefix();
    buf.append_fmt("%f", v);
}

void jwriter::val_bool(bool v)
{
    prefix();
    buf.append_str(v ? "true" : "false");
}

void jwriter::val_null()
{
    prefix();
    buf.append_str("null");
}

void jwriter::val_raw(const char* raw)
{
    prefix();
    buf.append_str(raw);
}

void jwriter::kv_str(const char* name, const char* v) { key(name); val_str(v); }
void jwriter::kv_i64(const char* name, long long v) { key(name); val_i64(v); }
void jwriter::kv_u64(const char* name, unsigned long long v) { key(name); val_u64(v); }
void jwriter::kv_bool(const char* name, bool v) { key(name); val_bool(v); }
void jwriter::kv_null(const char* name) { key(name); val_null(); }
void jwriter::kv_raw(const char* name, const char* raw) { key(name); val_raw(raw); }

void jwriter::kv_snowflake(const char* name, unsigned long long v)
{
    char tmp[32];
    cnprint(tmp, sizeof(tmp), "%llu", v);
    key(name);
    val_str(tmp);
}
