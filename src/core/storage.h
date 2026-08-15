#pragma once

// Credential storage. The token blob is sealed with a key derived from the
// machine fingerprint, so copying the file to another PC yields nothing: the
// derived key differs and the AEAD tag fails to verify.
// One remembered sign-in. The name and avatar are only what the account looked
// like when it was last used: they are there so the switcher has something to
// show before any of them has connected.
#include "net/proxy.h"

struct saved_account
{
    char token[256];
    char name[64];
    char avatar[64];          // hash, may be empty
    unsigned long long id;

    // Where this account's traffic goes, when it wants its own route. Most
    // do not: the common case is one proxy for the whole client, and only the
    // odd account out needs its own.
    bool proxy_override;
    proxy_config proxy;
};

namespace storage
{
    bool save_token(const char* token);
    bool load_token(char* out, int cap);
    void clear_token();
    bool has_token();

    // A short, stable name for an account's private folders: the id put
    // through the same machine bound hash the token seal uses. Two accounts
    // never collide, and a folder full of somebody's chat history does not
    // announce whose it is. Writes at most 33 bytes.
    void account_tag(unsigned long long id, char* out, int cap);

    // ---- accounts ----
    //
    // Kept in one file sealed the same way the single token was, so a copy of
    // it on another machine is still worthless. A token that was already stored
    // before this existed is picked up on the first load and becomes the first
    // entry, so nobody has to sign in again.
    void accounts_load();
    int accounts_count();
    const saved_account* account_at(int index);

    // Matches on user id, or on the token itself while the id is unknown.
    // Returns the index it ended up at, or -1.
    int account_remember(const char* token, unsigned long long id,
                         const char* name, const char* avatar);
    void account_forget(int index);

    // Which entry is signed in. -1 when none is.
    int active_account();
    void set_active_account(int index);
    void accounts_save();

    // Editing an account's proxy. The index is into the saved list. With
    // override off the account follows the default and cfg is kept only so
    // turning it back on remembers what was typed.
    void account_set_proxy(int index, const proxy_config* cfg, bool override_default);
    bool account_overrides_proxy(int index);

    // The route everything takes unless an account asks for its own. Kept in
    // the sealed file rather than settings.json, because it carries a
    // password.
    proxy_config default_proxy();
    void set_default_proxy(const proxy_config* cfg);

    // The proxy for an account id, or an unused config when there is none.
    // Callers hold this by value: the list moves when accounts are added.
    proxy_config account_proxy(unsigned long long id);
    proxy_config active_proxy();

    // Small key/value settings kept next to the token, in plain text.
    void settings_load();
    void settings_save();
    const char* settings_get(const char* key, const char* def);
    int settings_get_int(const char* key, int def);
    void settings_set(const char* key, const char* value);
    void settings_set_int(const char* key, int value);

    // Walking the whole table, for settings whose keys are not known in
    // advance - per person voice volumes are keyed by user id, so the only way
    // to find the ones that were saved is to look at what is there.
    int settings_count();
    const char* settings_key_at(int index);
    const char* settings_value_at(int index);
}
