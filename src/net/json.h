#pragma once
#include "uarena.h"
#include "ubuffer.h"

// Minimal DOM JSON reader/writer. Values are allocated inside a uarena, so a
// parsed document is released in one call and individual nodes never own
// memory. Gateway payloads are megabytes on READY, so parsing avoids per-node
// allocations by staging containers on a shared stack.

enum jtype : unsigned char
{
    JTYPE_NULL = 0,
    JTYPE_BOOL,
    JTYPE_NUM,
    JTYPE_STR,
    JTYPE_ARR,
    JTYPE_OBJ,
};

struct jval;

struct jmember
{
    const char* key;
    unsigned int key_len;
    jval* value;
};

struct jval
{
    jtype type;
    bool bval;
    unsigned int count;      // items in an array/object, bytes in a string
    double num;
    long long inum;          // exact integer when the literal had no '.'/'e'
    union
    {
        const char* sval;
        jval* items;         // JTYPE_ARR
        jmember* members;    // JTYPE_OBJ
    };

    // ---- object access ----
    const jval* get(const char* key) const;
    bool has(const char* key) const;

    const char* str(const char* key, const char* def = 0) const;
    long long i64(const char* key, long long def = 0) const;
    unsigned long long u64(const char* key, unsigned long long def = 0) const;
    int i32(const char* key, int def = 0) const;
    double dbl(const char* key, double def = 0.0) const;
    bool boolean(const char* key, bool def = false) const;
    // Snowflakes arrive as strings; this accepts either form.
    unsigned long long sf(const char* key) const;
    const jval* arr(const char* key) const;
    const jval* obj(const char* key) const;

    // ---- direct value access ----
    const char* as_str(const char* def = 0) const;
    long long as_i64(long long def = 0) const;
    unsigned long long as_u64(unsigned long long def = 0) const;
    double as_dbl(double def = 0.0) const;
    bool as_bool(bool def = false) const;
    unsigned long long as_snowflake() const;

    unsigned int size() const { return (type == JTYPE_ARR || type == JTYPE_OBJ) ? count : 0; }
    const jval* at(unsigned int index) const;
    const jmember* member_at(unsigned int index) const;

    bool is_null() const { return type == JTYPE_NULL; }
    bool valid() const { return type != JTYPE_NULL; }
};

// A parsed document. Owns the arena that every node points into.
struct jdoc
{
    uarena arena;
    jval* root;

    void init();
    bool parse(const char* text, int len = -1);
    void free_doc();

    const jval* r() const { return root ? root : null_value(); }
    static const jval* null_value();
};

// ---------------------------------------------------------------------------
// writer
// ---------------------------------------------------------------------------

// Comma bookkeeping is the only state a JSON writer really needs; nesting depth
// is tracked so nothing has to be balanced by hand at the call site.
struct jwriter
{
    ubuffer buf;
    unsigned int depth;
    bool need_comma[32];

    void init();
    void free_writer();

    void begin_obj();
    void end_obj();
    void begin_arr();
    void end_arr();

    void key(const char* name);

    void val_str(const char* v, int len = -1);
    void val_i64(long long v);
    void val_u64(unsigned long long v);
    void val_dbl(double v);
    void val_bool(bool v);
    void val_null();
    void val_raw(const char* raw);

    void kv_str(const char* name, const char* v);
    void kv_i64(const char* name, long long v);
    void kv_u64(const char* name, unsigned long long v);
    void kv_bool(const char* name, bool v);
    void kv_null(const char* name);
    // Snowflakes must go back out as strings.
    void kv_snowflake(const char* name, unsigned long long v);
    void kv_raw(const char* name, const char* raw);

    const char* c_str() { return buf.c_str(); }
    unsigned int size() const { return buf.size; }

private:
    void prefix();
};
