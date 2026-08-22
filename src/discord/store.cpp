#include "pch.h"
#include "store.h"
#include "core/log.h"

namespace
{
    uarena g_arena;
    CRITICAL_SECTION g_lock;
    bool g_ready = false;

    umap<snowflake, duser*> g_users;
    umap<snowflake, dchannel*> g_channels;
    umap<snowflake, dguild*> g_guilds;

    ulist<drelationship> g_relationships;
    ulist<dvoice_state> g_voice;
    ulist<snowflake> g_guild_order;
    ulist<snowflake> g_dm_order;

    snowflake g_self_id = 0;
    unsigned int g_revision = 1;

    // Interning table so repeated names (author of every message) share storage.
    struct intern_entry
    {
        unsigned __int64 hash;
        const char* text;
    };
    ulist<intern_entry> g_interned;
}

void store::init()
{
    if (g_ready) return;

    InitializeCriticalSection(&g_lock);
    g_arena.init();
    g_users = umap<snowflake, duser*>();
    g_channels = umap<snowflake, dchannel*>();
    g_guilds = umap<snowflake, dguild*>();
    g_relationships = ulist<drelationship>();
    g_voice = ulist<dvoice_state>();
    g_guild_order = ulist<snowflake>();
    g_dm_order = ulist<snowflake>();
    g_interned = ulist<intern_entry>();
    g_ready = true;
}

void store::reset()
{
    if (!g_ready) return;

    // Switching accounts leaves a cache belonging to somebody else: their
    // guilds, their names, their unread state. All of it goes, and the arena
    // that every interned string lives in goes with it, so nothing here may be
    // held across this call.
    EnterCriticalSection(&g_lock);

    g_users.dispose();
    g_channels.dispose();
    g_guilds.dispose();
    g_relationships.dispose();
    g_voice.dispose();
    g_guild_order.dispose();
    g_dm_order.dispose();
    g_interned.dispose();
    g_arena.reset();

    g_users = umap<snowflake, duser*>();
    g_channels = umap<snowflake, dchannel*>();
    g_guilds = umap<snowflake, dguild*>();
    g_relationships = ulist<drelationship>();
    g_voice = ulist<dvoice_state>();
    g_guild_order = ulist<snowflake>();
    g_dm_order = ulist<snowflake>();
    g_interned = ulist<intern_entry>();

    g_self_id = 0;
    g_revision++;

    LeaveCriticalSection(&g_lock);
}

void store::shutdown()
{
    if (!g_ready) return;

    EnterCriticalSection(&g_lock);
    g_users.dispose();
    g_channels.dispose();
    g_guilds.dispose();
    g_relationships.dispose();
    g_voice.dispose();
    g_guild_order.dispose();
    g_dm_order.dispose();
    g_interned.dispose();
    g_arena.reset();
    LeaveCriticalSection(&g_lock);

    DeleteCriticalSection(&g_lock);
    g_ready = false;
}

void store::lock() { if (g_ready) EnterCriticalSection(&g_lock); }
void store::unlock() { if (g_ready) LeaveCriticalSection(&g_lock); }

bool store::try_lock()
{
    return g_ready && TryEnterCriticalSection(&g_lock) != 0;
}

unsigned int store::revision() { return g_revision; }
void store::bump_revision() { g_revision++; }

const char* store::intern(const char* s, int len)
{
    if (!s) return 0;
    if (len < 0) len = (int)ccslenf(s);
    if (len == 0) return "";

    // Only short strings are worth deduplicating; message bodies are unique.
    if (len <= 64)
    {
        unsigned __int64 h = ccrc64(s, (unsigned int)len) ^ ((unsigned __int64)len << 56);
        for (unsigned int i = 0; i < g_interned.count; i++)
        {
            if (g_interned[i].hash == h && ccsncmp(g_interned[i].text, s, (size_t)len) == 0 &&
                g_interned[i].text[len] == 0)
                return g_interned[i].text;
        }
        const char* copy = g_arena.dup(s, len);
        intern_entry e;
        e.hash = h;
        e.text = copy;
        g_interned.push(e);
        return copy;
    }

    return g_arena.dup(s, len);
}

static const char* dup_field(const jval* obj, const char* key)
{
    const jval* v = obj->get(key);
    if (v->type != JTYPE_STR) return 0;
    return store::intern(v->sval, (int)v->count);
}

// ---------------------------------------------------------------------------
// identity
// ---------------------------------------------------------------------------

snowflake store::self_id() { return g_self_id; }
void store::set_self_id(snowflake id) { g_self_id = id; }
duser* store::self() { return find_user(g_self_id); }

// ---------------------------------------------------------------------------
// users
// ---------------------------------------------------------------------------

duser* store::find_user(snowflake id)
{
    duser** slot = g_users.find(id);
    return slot ? *slot : 0;
}

duser* store::upsert_user(const jval* v)
{
    if (!v || v->type != JTYPE_OBJ) return 0;

    snowflake id = v->sf("id");
    if (!id) return 0;

    duser* u = find_user(id);
    if (!u)
    {
        u = g_arena.make<duser>();
        if (!u) return 0;
        u->id = id;
        g_users.insert(id, u);
    }

    if (v->has("username")) u->username = dup_field(v, "username");
    if (v->has("global_name")) u->global_name = dup_field(v, "global_name");
    if (v->has("discriminator")) u->discriminator = dup_field(v, "discriminator");
    if (v->has("avatar")) u->avatar = dup_field(v, "avatar");
    if (v->has("banner")) u->banner = dup_field(v, "banner");
    if (v->has("bio")) u->bio = dup_field(v, "bio");
    if (v->has("accent_color")) u->accent_color = (unsigned int)v->i64("accent_color", 0);
    if (v->has("public_flags")) u->public_flags = v->i32("public_flags", 0);
    if (v->has("premium_type")) u->premium_type = v->i32("premium_type", 0);
    if (v->has("bot")) u->bot = v->boolean("bot", false);

    // Present only on the account's own object. dup_field returns null for a
    // JSON null, which is exactly what an unset phone/email means.
    if (v->has("email")) u->email = dup_field(v, "email");
    if (v->has("phone")) u->phone = dup_field(v, "phone");
    if (v->has("verified")) u->verified = v->boolean("verified", false);
    if (v->has("mfa_enabled")) u->mfa_enabled = v->boolean("mfa_enabled", false);

    return u;
}

// ---------------------------------------------------------------------------
// channels
// ---------------------------------------------------------------------------

dchannel* store::find_channel(snowflake id)
{
    dchannel** slot = g_channels.find(id);
    return slot ? *slot : 0;
}

namespace
{
    // "roles" on a member object is a flat array of role ids as strings.
    // Rewritten rather than merged: it is always the full set.
    void read_member_roles(dmember* m, const jval* src)
    {
        const jval* roles = src->arr("roles");
        if (roles->type != JTYPE_ARR) return;

        m->roles.clear_fast();
        for (unsigned int i = 0; i < roles->count; i++)
        {
            snowflake rid = roles->at(i)->as_snowflake();
            if (rid) m->roles.push(rid);
        }
    }

    void read_overwrites(dchannel* c, const jval* src)
    {
        const jval* list = src->arr("permission_overwrites");
        if (list->type != JTYPE_ARR) return;

        c->overwrites.clear_fast();
        for (unsigned int i = 0; i < list->count; i++)
        {
            const jval* o = list->at(i);
            doverwrite ow;
            ccfset(&ow, 0, sizeof(ow));
            ow.id = o->sf("id");
            ow.type = o->i32("type", 0);
            ow.allow = o->sf("allow");
            ow.deny = o->sf("deny");
            if (ow.id) c->overwrites.push(ow);
        }
    }
}

dchannel* store::upsert_channel(const jval* v, snowflake guild_id)
{
    if (!v || v->type != JTYPE_OBJ) return 0;

    snowflake id = v->sf("id");
    if (!id) return 0;

    dchannel* c = find_channel(id);
    if (!c)
    {
        c = g_arena.make<dchannel>();
        if (!c) return 0;
        c->id = id;
        c->messages = ulist<dmessage>();
        c->recipients = ulist<snowflake>();
        c->overwrites = ulist<doverwrite>();
        g_channels.insert(id, c);
    }

    c->type = v->i32("type", c->type);
    if (guild_id) c->guild_id = guild_id;
    if (v->has("guild_id")) c->guild_id = v->sf("guild_id");
    if (v->has("parent_id")) c->parent_id = v->sf("parent_id");
    if (v->has("last_message_id")) c->last_message_id = v->sf("last_message_id");
    if (v->has("owner_id")) c->owner_id = v->sf("owner_id");
    if (v->has("name")) c->name = dup_field(v, "name");
    if (v->has("topic")) c->topic = dup_field(v, "topic");
    if (v->has("icon")) c->icon = dup_field(v, "icon");
    if (v->has("position")) c->position = v->i32("position", 0);
    if (v->has("user_limit")) c->user_limit = v->i32("user_limit", 0);
    if (v->has("bitrate")) c->bitrate = v->i32("bitrate", 0);
    if (v->has("rate_limit_per_user")) c->rate_limit_per_user = v->i32("rate_limit_per_user", 0);
    if (v->has("nsfw")) c->nsfw = v->boolean("nsfw", false);

    // Threads carry their state in a nested object.
    const jval* meta = v->obj("thread_metadata");
    if (meta && meta->type == JTYPE_OBJ)
    {
        c->archived = meta->boolean("archived", false);
        c->locked = meta->boolean("locked", false);
    }
    if (v->has("member_count")) c->member_count = v->i32("member_count", 0);
    if (v->has("message_count")) c->message_count = v->i32("message_count", 0);

    // A DM carries either full user objects in "recipients" (REST, older
    // gateway payloads) or bare ids in "recipient_ids", with the users listed
    // separately in READY. Both shapes have to be understood.
    const jval* recips = v->arr("recipients");
    if (recips->type == JTYPE_ARR)
    {
        c->recipients.clear_fast();
        for (unsigned int i = 0; i < recips->count; i++)
        {
            duser* ru = upsert_user(recips->at(i));
            if (ru) c->recipients.push(ru->id);
        }
    }
    else
    {
        const jval* ids = v->arr("recipient_ids");
        if (ids->type == JTYPE_ARR)
        {
            c->recipients.clear_fast();
            for (unsigned int i = 0; i < ids->count; i++)
            {
                snowflake rid = ids->at(i)->as_snowflake();
                if (rid) c->recipients.push(rid);
            }
        }
    }

    read_overwrites(c, v);

    if (c->is_dm()) touch_dm_order();
    return c;
}

void store::remove_channel(snowflake id)
{
    dchannel* c = find_channel(id);
    if (!c) return;

    g_channels.erase(id);

    for (unsigned int i = 0; i < g_dm_order.count; i++)
    {
        if (g_dm_order[i] == id) { g_dm_order.delete_at(i); break; }
    }

    dguild* g = find_guild(c->guild_id);
    if (g)
    {
        for (unsigned int i = 0; i < g->channels.count; i++)
            if (g->channels[i] == id) { g->channels.delete_at(i); break; }
    }
    bump_revision();
}

// ---------------------------------------------------------------------------
// guilds
// ---------------------------------------------------------------------------

dguild* store::find_guild(snowflake id)
{
    dguild** slot = g_guilds.find(id);
    return slot ? *slot : 0;
}

dguild* store::upsert_guild(const jval* v)
{
    if (!v || v->type != JTYPE_OBJ) return 0;

    snowflake id = v->sf("id");
    if (!id) return 0;

    dguild* g = find_guild(id);
    if (!g)
    {
        g = g_arena.make<dguild>();
        if (!g) return 0;
        g->id = id;
        g->channels = ulist<snowflake>();
        g->members = ulist<dmember>();
        g->roles = ulist<drole>();
        g_guilds.insert(id, g);
        g_guild_order.push(id);
    }

    if (v->has("name")) g->name = dup_field(v, "name");
    if (v->has("icon")) g->icon = dup_field(v, "icon");
    if (v->has("owner_id")) g->owner_id = v->sf("owner_id");
    if (v->has("joined_at")) g->joined_at = dup_field(v, "joined_at");
    if (v->has("vanity_url_code")) g->vanity_url_code = dup_field(v, "vanity_url_code");
    if (v->has("description")) g->description = dup_field(v, "description");
    if (v->has("verification_level")) g->verification_level = v->i32("verification_level", 0);
    if (v->has("premium_tier")) g->premium_tier = v->i32("premium_tier", 0);
    if (v->has("premium_subscription_count"))
        g->premium_subscribers = v->i32("premium_subscription_count", 0);

    const jval* roles = v->arr("roles");
    if (roles->type == JTYPE_ARR && roles->count)
    {
        g->roles.clear_fast();
        for (unsigned int i = 0; i < roles->count; i++)
        {
            const jval* r = roles->at(i);
            drole role;
            ccfset(&role, 0, sizeof(role));
            role.id = r->sf("id");
            role.name = dup_field(r, "name");
            role.color = (unsigned int)r->i64("color", 0);
            role.position = r->i32("position", 0);
            role.permissions = r->sf("permissions");   // arrives as a string
            role.hoist = r->boolean("hoist", false);
            role.mentionable = r->boolean("mentionable", false);
            g->roles.push(role);
        }
    }

    const jval* channels = v->arr("channels");
    if (channels->type == JTYPE_ARR && channels->count)
    {
        g->channels.clear_fast();
        for (unsigned int i = 0; i < channels->count; i++)
        {
            dchannel* c = upsert_channel(channels->at(i), id);
            if (c) g->channels.push(c->id);
        }
        g->loaded = true;
        sort_guild_channels(g);
    }

    const jval* members = v->arr("members");
    if (members->type == JTYPE_ARR && members->count)
    {
        for (unsigned int i = 0; i < members->count; i++)
        {
            const jval* m = members->at(i);
            duser* mu = upsert_user(m->obj("user"));
            if (!mu) continue;

            bool exists = false;
            for (unsigned int k = 0; k < g->members.count; k++)
            {
                if (g->members[k].user_id == mu->id) { exists = true; break; }
            }
            if (exists) continue;

            dmember mem;
            ccfset(&mem, 0, sizeof(mem));
            mem.user_id = mu->id;
            mem.nick = dup_field(m, "nick");
            mem.timeout_until_ms = iso_to_unix_ms(m->str("communication_disabled_until", 0));
            mem.roles = ulist<snowflake>();
            read_member_roles(&mem, m);
            g->members.push(mem);
        }
    }

    const jval* vstates = v->arr("voice_states");
    if (vstates->type == JTYPE_ARR)
    {
        for (unsigned int i = 0; i < vstates->count; i++)
            set_voice_state(vstates->at(i), id);
    }

    const jval* presences = v->arr("presences");
    for (unsigned int i = 0; i < presences->count; i++)
        apply_presence(presences->at(i));

    bump_revision();
    return g;
}

// Discord hands channels over in whatever order it likes and leaves the
// ordering to the client. Every other client shows the same thing: categories
// by position, and inside each one the text channels before the voice ones,
// each group by its own position. Getting this wrong is not subtle - the
// sidebar simply looks wrong to anybody who has used discord.
void store::upsert_role(dguild* g, const jval* role)
{
    if (!g || !role || role->type != JTYPE_OBJ) return;

    snowflake id = role->sf("id");
    if (!id) return;

    drole* found = 0;
    for (unsigned int i = 0; i < g->roles.count; i++)
        if (g->roles[i].id == id) { found = &g->roles[i]; break; }

    if (!found)
    {
        drole fresh;
        ccfset(&fresh, 0, sizeof(fresh));
        fresh.id = id;
        g->roles.push(fresh);
        found = &g->roles[g->roles.count - 1];
    }

    found->name = dup_field(role, "name");
    found->color = (unsigned int)role->i64("color", 0);
    found->position = role->i32("position", 0);
    found->permissions = role->sf("permissions");
    found->hoist = role->boolean("hoist", false);
    found->mentionable = role->boolean("mentionable", false);

    bump_revision();
}

void store::remove_role(dguild* g, snowflake role_id)
{
    if (!g || !role_id) return;

    for (unsigned int i = 0; i < g->roles.count; i++)
    {
        if (g->roles[i].id != role_id) continue;
        g->roles.delete_at((int)i);
        break;
    }

    // And off everybody who had it, because the server will not send a member
    // update for each of them - a deleted role simply stops existing, and a
    // member list still naming it would colour and sort people by nothing.
    for (unsigned int i = 0; i < g->members.count; i++)
    {
        ulist<snowflake>* roles = &g->members[i].roles;
        for (unsigned int k = 0; k < roles->count; k++)
        {
            if ((*roles)[k] != role_id) continue;
            roles->delete_at((int)k);
            break;
        }
    }

    bump_revision();
}

void store::add_guild_member(dguild* g, const jval* member)
{
    if (!g || !member || member->type != JTYPE_OBJ) return;

    duser* u = upsert_user(member->obj("user"));

    // READY's merged_members name the person by id alone - the user objects
    // themselves are in a list of their own. A member without a user object
    // is still worth keeping: the roles on it are what decide what this
    // account can see.
    snowflake uid = u ? u->id : member->sf("user_id");
    if (!uid) return;

    // The member list is where presence for a server's people arrives; there is
    // no separate dispatch for most of them.
    const jval* presence = member->obj("presence");
    if (presence && presence->type == JTYPE_OBJ) apply_presence(presence);

    const char* nick = member->str("nick", 0);

    // Absent means "no timeout" here rather than "unchanged": discord clears a
    // timeout by sending the field as null, and reading that as nothing to do
    // would leave a lifted timeout showing forever.
    unsigned long long until = 0;
    if (member->has("communication_disabled_until"))
        until = iso_to_unix_ms(member->str("communication_disabled_until", 0));

    for (unsigned int i = 0; i < g->members.count; i++)
    {
        if (g->members[i].user_id != uid) continue;
        if (nick) g->members[i].nick = intern(nick);
        if (member->has("communication_disabled_until")) g->members[i].timeout_until_ms = until;
        read_member_roles(&g->members[i], member);
        return;
    }

    dmember fresh;
    ccfset(&fresh, 0, sizeof(fresh));
    fresh.user_id = uid;
    fresh.nick = nick ? intern(nick) : 0;
    fresh.timeout_until_ms = until;
    fresh.roles = ulist<snowflake>();
    read_member_roles(&fresh, member);
    g->members.push(fresh);
}

const drole* store::find_role(const dguild* g, snowflake role_id)
{
    if (!g) return 0;
    for (unsigned int i = 0; i < g->roles.count; i++)
        if (g->roles[i].id == role_id) return &g->roles[i];
    return 0;
}

dmember* store::find_member(dguild* g, snowflake user_id)
{
    if (!g) return 0;
    for (unsigned int i = 0; i < g->members.count; i++)
        if (g->members[i].user_id == user_id) return &g->members[i];
    return 0;
}

const drole* store::member_color_role(const dguild* g, const dmember* m)
{
    if (!g || !m) return 0;

    const drole* best = 0;
    for (unsigned int i = 0; i < m->roles.count; i++)
    {
        const drole* r = find_role(g, m->roles[i]);
        if (!r || !r->color) continue;          // uncoloured roles do not count
        if (!best || r->position > best->position) best = r;
    }
    return best;
}

const drole* store::member_hoist_role(const dguild* g, const dmember* m)
{
    if (!g || !m) return 0;

    const drole* best = 0;
    for (unsigned int i = 0; i < m->roles.count; i++)
    {
        const drole* r = find_role(g, m->roles[i]);
        if (!r || !r->hoist) continue;
        if (!best || r->position > best->position) best = r;
    }
    return best;
}

unsigned long long store::member_permissions(const dguild* g, snowflake user_id,
                                             const dchannel* c)
{
    if (!g) return 0;

    // The owner is not subject to any of this.
    if (g->owner_id && g->owner_id == user_id) return ~0ULL;

    // @everyone is a real role whose id is the guild's own, and every member
    // holds it whether or not it is listed on them.
    unsigned long long base = 0;
    const drole* everyone = find_role(g, g->id);
    if (everyone) base = everyone->permissions;

    const dmember* m = 0;
    for (unsigned int i = 0; i < g->members.count; i++)
        if (g->members[i].user_id == user_id) { m = &g->members[i]; break; }

    if (m)
    {
        for (unsigned int i = 0; i < m->roles.count; i++)
        {
            const drole* r = find_role(g, m->roles[i]);
            if (r) base |= r->permissions;
        }
    }

    if (base & PERM_ADMINISTRATOR) return ~0ULL;
    if (!c) return base;

    // The channel table, in the order discord applies it: @everyone first,
    // then every role the member holds accumulated together, then the entry
    // naming the member. A deny only loses to an allow further down the list.
    for (unsigned int i = 0; i < c->overwrites.count; i++)
    {
        const doverwrite* o = &c->overwrites[i];
        if (o->type != 0 || o->id != g->id) continue;
        base &= ~o->deny;
        base |= o->allow;
    }

    if (m)
    {
        unsigned long long allow = 0, deny = 0;
        for (unsigned int i = 0; i < c->overwrites.count; i++)
        {
            const doverwrite* o = &c->overwrites[i];
            if (o->type != 0 || o->id == g->id) continue;

            bool held = false;
            for (unsigned int k = 0; k < m->roles.count && !held; k++)
                if (m->roles[k] == o->id) held = true;
            if (!held) continue;

            allow |= o->allow;
            deny |= o->deny;
        }
        base &= ~deny;
        base |= allow;
    }

    for (unsigned int i = 0; i < c->overwrites.count; i++)
    {
        const doverwrite* o = &c->overwrites[i];
        if (o->type != 1 || o->id != user_id) continue;
        base &= ~o->deny;
        base |= o->allow;
    }

    return base;
}

// The position of the highest role somebody holds. @everyone sits at zero, so
// a member with no roles of their own compares as zero too.
static int top_role_position(const dguild* g, snowflake user_id)
{
    const dmember* m = 0;
    for (unsigned int i = 0; i < g->members.count; i++)
        if (g->members[i].user_id == user_id) { m = &g->members[i]; break; }

    if (!m) return 0;

    int best = 0;
    for (unsigned int i = 0; i < m->roles.count; i++)
    {
        const drole* r = store::find_role(g, m->roles[i]);
        if (r && r->position > best) best = r->position;
    }
    return best;
}

bool store::outranks(const dguild* g, snowflake actor_id, snowflake target_id)
{
    if (!g || !actor_id || !target_id || actor_id == target_id) return false;

    // The owner outranks everybody, and nobody outranks the owner - not even
    // an administrator, which is the one case where holding every permission
    // is still not enough.
    if (g->owner_id == actor_id) return true;
    if (g->owner_id == target_id) return false;

    return top_role_position(g, actor_id) > top_role_position(g, target_id);
}

bool store::can_view_channel(const dguild* g, snowflake user_id, const dchannel* c)
{
    // Nothing to go on yet - before the channel's table has arrived, hiding
    // everybody would look like a bug rather than like privacy.
    if (!g || !c) return true;
    if (!c->overwrites.count && !g->roles.count) return true;

    // Nor is anything decidable while this account's own roles are unknown.
    // Working from @everyone alone would call every role-gated channel closed,
    // which on most servers means all of them.
    bool known = false;
    for (unsigned int i = 0; i < g->members.count && !known; i++)
        if (g->members[i].user_id == user_id) known = true;
    if (!known) return true;

    return (member_permissions(g, user_id, c) & PERM_VIEW_CHANNEL) != 0;
}

void store::sort_guild_channels(dguild* g)
{
    if (!g) return;

    struct key
    {
        int category;      // position of the parent category, -1 when loose
        snowflake parent;  // keeps two categories of equal position apart
        int voice;         // text-like first, voice after
        int position;
        snowflake id;
    };

    // Small lists and an insertion sort: a guild with a thousand channels does
    // not exist, and this runs once per guild rather than per frame.
    for (unsigned int i = 1; i < g->channels.count; i++)
    {
        snowflake moving = g->channels[i];
        dchannel* mc = find_channel(moving);
        if (!mc) continue;

        dchannel* mp = mc->parent_id ? find_channel(mc->parent_id) : 0;
        key a;
        a.category = mp ? mp->position : -1;
        a.parent = mc->parent_id;
        a.voice = mc->is_voice() ? 1 : 0;
        a.position = mc->position;
        a.id = mc->id;

        unsigned int at = i;
        while (at > 0)
        {
            dchannel* oc = find_channel(g->channels[at - 1]);
            if (!oc) break;

            dchannel* op = oc->parent_id ? find_channel(oc->parent_id) : 0;
            key b;
            b.category = op ? op->position : -1;
            b.parent = oc->parent_id;
            b.voice = oc->is_voice() ? 1 : 0;
            b.position = oc->position;
            b.id = oc->id;

            bool before = false;
            if (a.category != b.category)      before = a.category < b.category;
            else if (a.parent != b.parent)     before = a.parent < b.parent;
            else if (a.voice != b.voice)       before = a.voice < b.voice;
            else if (a.position != b.position) before = a.position < b.position;
            else                               before = a.id < b.id;

            if (!before) break;

            g->channels[at] = g->channels[at - 1];
            at--;
        }
        g->channels[at] = moving;
    }
}

void store::remove_guild(snowflake id)
{
    dguild* g = find_guild(id);
    if (!g) return;

    for (unsigned int i = 0; i < g->channels.count; i++)
        g_channels.erase(g->channels[i]);

    g_guilds.erase(id);
    for (unsigned int i = 0; i < g_guild_order.count; i++)
        if (g_guild_order[i] == id) { g_guild_order.delete_at(i); break; }

    bump_revision();
}

// ---------------------------------------------------------------------------
// messages
// ---------------------------------------------------------------------------

dmessage* store::find_message(dchannel* ch, snowflake id)
{
    if (!ch) return 0;
    for (unsigned int i = ch->messages.count; i > 0; i--)
    {
        if (ch->messages[i - 1].id == id) return &ch->messages[i - 1];
    }
    return 0;
}

static void parse_attachments(const jval* v, dmessage* msg)
{
    const jval* list = v->arr("attachments");
    if (list->type != JTYPE_ARR) return;

    msg->attachments.clear_fast();
    for (unsigned int i = 0; i < list->count; i++)
    {
        const jval* a = list->at(i);
        dattachment at;
        ccfset(&at, 0, sizeof(at));
        at.id = a->sf("id");
        at.filename = store::intern(a->str("filename", ""));
        at.url = store::intern(a->str("url", ""));
        at.proxy_url = store::intern(a->str("proxy_url", ""));
        at.content_type = store::intern(a->str("content_type", ""));
        at.size = (unsigned int)a->i64("size", 0);
        at.width = a->i32("width", 0);
        at.height = a->i32("height", 0);
        msg->attachments.push(at);
    }
}

static void parse_embeds(const jval* v, dmessage* msg)
{
    const jval* list = v->arr("embeds");
    if (list->type != JTYPE_ARR) return;

    msg->embeds.clear_fast();
    for (unsigned int i = 0; i < list->count; i++)
    {
        const jval* e = list->at(i);
        dembed em;
        ccfset(&em, 0, sizeof(em));
        em.title = store::intern(e->str("title", 0));
        em.description = store::intern(e->str("description", 0));
        em.url = store::intern(e->str("url", 0));
        em.color = (unsigned int)e->i64("color", 0);

        // Both addresses are kept: the proxy is the polite one to ask, but it
        // returns a single frame for anything animated, so the original has to
        // stay available.
        const jval* img = e->obj("image");
        if (img->type == JTYPE_OBJ)
        {
            em.image_src = store::intern(img->str("url", 0));
            em.image_url = store::intern(img->str("proxy_url", 0));
            if (!em.image_url) em.image_url = em.image_src;
            em.image_w = img->i32("width", 0);
            em.image_h = img->i32("height", 0);
        }
        const jval* thumb = e->obj("thumbnail");
        if (thumb->type == JTYPE_OBJ)
        {
            em.thumbnail_src = store::intern(thumb->str("url", 0));
            em.thumbnail_url = store::intern(thumb->str("proxy_url", 0));
            if (!em.thumbnail_url) em.thumbnail_url = em.thumbnail_src;
            if (!em.image_url)
            {
                em.image_w = thumb->i32("width", 0);
                em.image_h = thumb->i32("height", 0);
            }
        }
        const jval* author = e->obj("author");
        if (author->type == JTYPE_OBJ) em.author_name = store::intern(author->str("name", 0));
        const jval* footer = e->obj("footer");
        if (footer->type == JTYPE_OBJ) em.footer = store::intern(footer->str("text", 0));

        msg->embeds.push(em);
    }
}

static void parse_reactions(const jval* v, dmessage* msg)
{
    const jval* list = v->arr("reactions");
    if (list->type != JTYPE_ARR) return;

    msg->reactions.clear_fast();
    for (unsigned int i = 0; i < list->count; i++)
    {
        const jval* r = list->at(i);
        const jval* emoji = r->obj("emoji");

        dreaction re;
        ccfset(&re, 0, sizeof(re));
        re.emoji_name = store::intern(emoji->str("name", "?"));
        re.emoji_id = emoji->sf("id");
        re.count = r->i32("count", 0);
        re.me = r->boolean("me", false);
        msg->reactions.push(re);
    }
}

dmessage* store::upsert_message(const jval* v)
{
    if (!v || v->type != JTYPE_OBJ) return 0;

    snowflake id = v->sf("id");
    snowflake channel_id = v->sf("channel_id");
    if (!id || !channel_id) return 0;

    dchannel* ch = find_channel(channel_id);
    if (!ch)
    {
        // A DM can arrive before its channel object does; keep a stub so the
        // message is not dropped.
        ch = g_arena.make<dchannel>();
        if (!ch) return 0;
        ch->id = channel_id;
        ch->type = CH_DM;
        ch->messages = ulist<dmessage>();
        ch->recipients = ulist<snowflake>();
        g_channels.insert(channel_id, ch);
    }

    duser* author = upsert_user(v->obj("author"));

    // A direct message whose channel object never arrived has nobody in it,
    // and a conversation with nobody in it is drawn as "без названия". The
    // person who wrote is right here in the message, so there is no need to
    // wait for the channel object or ask for it.
    //
    // This is the ordinary case for a bot: discord sends a bot no private
    // channels in READY and offers no way to list them, so every direct
    // conversation a bot has is one that arrived exactly like this.
    if (ch->is_dm() && !ch->recipients.count && author && author->id != g_self_id)
        ch->recipients.push(author->id);

    // Everybody the message points at, remembered here rather than looked up
    // later. On the wire a mention is only an id, and the person behind it may
    // never have said anything in this channel - without this the chat has
    // nothing to put in the tag but the number.
    {
        const jval* mentioned = v->arr("mentions");
        for (unsigned int i = 0; i < mentioned->count; i++)
            upsert_user(mentioned->at(i));
    }

    dmessage* msg = find_message(ch, id);
    if (!msg)
    {
        dmessage fresh;
        ccfset(&fresh, 0, sizeof(fresh));
        fresh.id = id;
        fresh.channel_id = channel_id;
        fresh.attachments = ulist<dattachment>();
        fresh.embeds = ulist<dembed>();
        fresh.reactions = ulist<dreaction>();

        // Keep the list ordered by id so rendering is a straight walk.
        unsigned int pos = ch->messages.count;
        while (pos > 0 && ch->messages[pos - 1].id > id) pos--;

        ch->messages.push(fresh);
        for (unsigned int i = ch->messages.count - 1; i > pos; i--)
            ch->messages[i] = ch->messages[i - 1];
        ch->messages[pos] = fresh;
        msg = &ch->messages[pos];
    }

    msg->guild_id = v->has("guild_id") ? v->sf("guild_id") : ch->guild_id;
    if (author) msg->author_id = author->id;
    if (v->has("content")) msg->content = intern(v->str("content", ""));
    if (v->has("timestamp")) msg->timestamp = intern(v->str("timestamp", ""));
    if (v->has("edited_timestamp")) msg->edited_timestamp = intern(v->str("edited_timestamp", 0));
    msg->type = v->i32("type", msg->type);
    msg->pending = false;
    msg->failed = false;

    const jval* ref = v->obj("referenced_message");
    if (ref->type == JTYPE_OBJ) msg->referenced_id = ref->sf("id");
    else if (v->has("message_reference")) msg->referenced_id = v->obj("message_reference")->sf("message_id");

    parse_attachments(v, msg);
    parse_embeds(v, msg);
    parse_reactions(v, msg);

    if (id > ch->last_message_id) ch->last_message_id = id;
    if (ch->is_dm()) touch_dm_order();

    return msg;
}

void store::mark_message_deleted(snowflake channel_id, snowflake message_id)
{
    dchannel* c = find_channel(channel_id);
    dmessage* m = c ? find_message(c, message_id) : 0;
    if (m) m->deleted = true;
}

void store::remove_message(snowflake channel_id, snowflake message_id)
{
    dchannel* ch = find_channel(channel_id);
    if (!ch) return;

    for (unsigned int i = 0; i < ch->messages.count; i++)
    {
        if (ch->messages[i].id == message_id)
        {
            ch->messages[i].attachments.dispose();
            ch->messages[i].embeds.dispose();
            ch->messages[i].reactions.dispose();
            ch->messages.delete_at(i);
            return;
        }
    }
}

dmessage* store::add_pending_message(snowflake channel_id, const char* content, snowflake local_id)
{
    dchannel* ch = find_channel(channel_id);
    if (!ch) return 0;

    dmessage msg;
    ccfset(&msg, 0, sizeof(msg));
    msg.id = local_id;
    msg.channel_id = channel_id;
    msg.guild_id = ch->guild_id;
    msg.author_id = g_self_id;
    msg.content = intern(content);
    msg.pending = true;
    msg.attachments = ulist<dattachment>();
    msg.embeds = ulist<dembed>();
    msg.reactions = ulist<dreaction>();

    ch->messages.push(msg);
    return &ch->messages[ch->messages.count - 1];
}

// ---------------------------------------------------------------------------
// presence
// ---------------------------------------------------------------------------

void store::apply_presence(const jval* p)
{
    if (!p || p->type != JTYPE_OBJ) return;

    snowflake uid = p->sf("user_id");
    if (!uid) uid = p->obj("user")->sf("id");
    if (!uid) return;

    duser* u = find_user(uid);
    if (!u) return;

    const char* s = p->str("status", 0);
    if (!s) return;

    if (ccscmp(s, "online") == 0) u->status = STATUS_ONLINE;
    else if (ccscmp(s, "idle") == 0) u->status = STATUS_IDLE;
    else if (ccscmp(s, "dnd") == 0) u->status = STATUS_DND;
    else u->status = STATUS_OFFLINE;
}

// ---------------------------------------------------------------------------
// relationships
// ---------------------------------------------------------------------------

void store::set_relationship(snowflake user_id, int type, const char* nickname)
{
    for (unsigned int i = 0; i < g_relationships.count; i++)
    {
        if (g_relationships[i].user_id == user_id)
        {
            g_relationships[i].type = type;
            if (nickname) g_relationships[i].nickname = intern(nickname);
            bump_revision();
            return;
        }
    }

    drelationship r;
    ccfset(&r, 0, sizeof(r));
    r.user_id = user_id;
    r.type = type;
    r.nickname = nickname ? intern(nickname) : 0;
    g_relationships.push(r);
    bump_revision();
}

void store::remove_relationship(snowflake user_id)
{
    for (unsigned int i = 0; i < g_relationships.count; i++)
    {
        if (g_relationships[i].user_id == user_id)
        {
            g_relationships.delete_at(i);
            bump_revision();
            return;
        }
    }
}

int store::relationship_type(snowflake user_id)
{
    for (unsigned int i = 0; i < g_relationships.count; i++)
        if (g_relationships[i].user_id == user_id) return g_relationships[i].type;
    return REL_NONE;
}

const ulist<drelationship>& store::relationships() { return g_relationships; }

// ---------------------------------------------------------------------------
// voice
// ---------------------------------------------------------------------------

void store::set_voice_state(const jval* v, snowflake guild_id)
{
    if (!v || v->type != JTYPE_OBJ) return;

    snowflake user_id = v->sf("user_id");
    if (!user_id) return;

    snowflake channel_id = v->sf("channel_id");

    for (unsigned int i = 0; i < g_voice.count; i++)
    {
        if (g_voice[i].user_id != user_id) continue;

        if (!channel_id)
        {
            g_voice.delete_at(i);
            bump_revision();
            return;
        }
        g_voice[i].channel_id = channel_id;
        g_voice[i].guild_id = guild_id ? guild_id : v->sf("guild_id");
        g_voice[i].self_mute = v->boolean("self_mute", false);
        g_voice[i].self_deaf = v->boolean("self_deaf", false);
        g_voice[i].mute = v->boolean("mute", false);
        g_voice[i].deaf = v->boolean("deaf", false);
        g_voice[i].self_stream = v->boolean("self_stream", false);
        bump_revision();
        return;
    }

    if (!channel_id) return;

    dvoice_state st;
    ccfset(&st, 0, sizeof(st));
    st.user_id = user_id;
    st.channel_id = channel_id;
    st.guild_id = guild_id ? guild_id : v->sf("guild_id");
    st.self_mute = v->boolean("self_mute", false);
    st.self_deaf = v->boolean("self_deaf", false);
    st.mute = v->boolean("mute", false);
    st.deaf = v->boolean("deaf", false);
    st.self_stream = v->boolean("self_stream", false);
    g_voice.push(st);
    bump_revision();
}

const dvoice_state* store::find_voice_state(snowflake user_id)
{
    for (unsigned int i = 0; i < g_voice.count; i++)
        if (g_voice[i].user_id == user_id) return &g_voice[i];
    return 0;
}

namespace
{
    // One entry per call that is ringing somebody. A handful at most - a
    // person is not in twenty calls - so a list walked linearly is the whole
    // of what this needs.
    struct ringing_call
    {
        snowflake channel_id;
        unsigned int count;
    };

    ulist<ringing_call> g_ringing;

}

void store::set_call_ringing(snowflake channel_id, const ulist<snowflake>* users)
{
    if (!channel_id) return;

    unsigned int count = users ? users->count : 0;

    for (unsigned int i = 0; i < g_ringing.count; i++)
    {
        if (g_ringing[i].channel_id != channel_id) continue;

        if (!count) g_ringing.delete_at(i);
        else        g_ringing[i].count = count;
        return;
    }

    if (!count) return;

    ringing_call fresh;
    fresh.channel_id = channel_id;
    fresh.count = count;
    g_ringing.push(fresh);
}

void store::clear_call_ringing(snowflake channel_id)
{

    for (unsigned int i = g_ringing.count; i > 0; i--)
        if (g_ringing[i - 1].channel_id == channel_id) g_ringing.delete_at(i - 1);
}

bool store::call_is_ringing(snowflake channel_id)
{

    for (unsigned int i = 0; i < g_ringing.count; i++)
        if (g_ringing[i].channel_id == channel_id) return true;
    return false;
}

void store::clear_voice_states_for_channel(snowflake channel_id)
{
    for (unsigned int i = g_voice.count; i > 0; i--)
        if (g_voice[i - 1].channel_id == channel_id) g_voice.delete_at(i - 1);
    bump_revision();
}

const ulist<dvoice_state>& store::voice_states() { return g_voice; }

void store::users_in_voice(snowflake channel_id, ulist<snowflake>* out)
{
    out->clear_fast();
    for (unsigned int i = 0; i < g_voice.count; i++)
        if (g_voice[i].channel_id == channel_id) out->push(g_voice[i].user_id);
}

// ---------------------------------------------------------------------------
// ordering
// ---------------------------------------------------------------------------

void store::all_users(ulist<snowflake>* out)
{
    out->clear_fast();
    for (unsigned int i = 0; i < g_users.size(); i++)
    {
        duser* u = g_users.value_at(i);
        if (u) out->push(u->id);
    }
}

void store::all_channels(ulist<snowflake>* out)
{
    out->clear_fast();
    for (unsigned int i = 0; i < g_channels.size(); i++)
    {
        dchannel* c = g_channels.value_at(i);
        if (c) out->push(c->id);
    }
}

const ulist<snowflake>& store::guild_order() { return g_guild_order; }

// ---------------------------------------------------------------------------
// auth sessions
// ---------------------------------------------------------------------------

namespace
{
    ulist<dsession> g_sessions;
    int g_sessions_state = 0;
}

int store::sessions_state() { return g_sessions_state; }
void store::set_sessions_loading() { g_sessions_state = 1; }
void store::set_sessions_failed() { g_sessions_state = 3; }

void store::set_sessions(const jval* arr)
{
    g_sessions.dispose();
    g_sessions = ulist<dsession>();

    if (arr && arr->type == JTYPE_ARR)
    {
        for (unsigned int i = 0; i < arr->count; i++)
        {
            const jval* s = arr->at(i);

            dsession row;
            ccfset(&row, 0, sizeof(row));
            ccstrncpy(row.id_hash, s->str("id_hash", ""), sizeof(row.id_hash) - 1);
            ccstrncpy(row.last_used, s->str("approx_last_used_time", ""), sizeof(row.last_used) - 1);

            const jval* ci = s->obj("client_info");
            if (ci->type == JTYPE_OBJ)
            {
                ccstrncpy(row.os, ci->str("os", ""), sizeof(row.os) - 1);
                ccstrncpy(row.platform, ci->str("platform", ""), sizeof(row.platform) - 1);
                ccstrncpy(row.location, ci->str("location", ""), sizeof(row.location) - 1);
            }

            g_sessions.push(row);
        }
    }

    g_sessions_state = 2;
}

const ulist<dsession>& store::sessions() { return g_sessions; }

void store::touch_dm_order()
{
    g_dm_order.clear_fast();

    for (umap_pair<snowflake, dchannel*>* it = g_channels.begin(); it != g_channels.end(); ++it)
    {
        dchannel* c = it->value;
        if (c && c->is_dm()) g_dm_order.push(c->id);
    }

    // Most recent conversation first.
    for (unsigned int i = 1; i < g_dm_order.count; i++)
    {
        snowflake key = g_dm_order[i];
        dchannel* kc = find_channel(key);
        unsigned int j = i;
        while (j > 0)
        {
            dchannel* pc = find_channel(g_dm_order[j - 1]);
            if (pc && kc && pc->last_message_id >= kc->last_message_id) break;
            g_dm_order[j] = g_dm_order[j - 1];
            j--;
        }
        g_dm_order[j] = key;
    }

    bump_revision();
}

const ulist<snowflake>& store::dm_order() { return g_dm_order; }

// The one-to-one conversation with somebody, if there is one. Groups are not
// it: a message meant for one person does not belong in a room.
snowflake store::dm_with(snowflake user_id)
{
    for (unsigned int i = 0; i < g_dm_order.count; i++)
    {
        dchannel* c = find_channel(g_dm_order[i]);
        if (!c || c->type != CH_DM || c->recipients.count != 1) continue;
        if (c->recipients[0] == user_id) return c->id;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// read state
// ---------------------------------------------------------------------------

void store::apply_read_state(const jval* entry)
{
    if (!entry || entry->type != JTYPE_OBJ) return;

    snowflake id = entry->sf("id");
    if (!id) return;

    dchannel* c = find_channel(id);
    if (!c) return;

    c->last_read_id = entry->sf("last_message_id");
    c->mention_count = entry->i32("mention_count", 0);
}

namespace
{
    snowflake g_open_channel = 0;
}

void store::set_open_channel(snowflake channel_id)
{
    g_open_channel = channel_id;
}

void store::note_incoming(const jval* message)
{
    if (!message || message->type != JTYPE_OBJ) return;

    snowflake channel_id = message->sf("channel_id");
    dchannel* c = find_channel(channel_id);
    if (!c) return;

    snowflake author = message->obj("author")->sf("id");
    if (author && author == self_id())
    {
        // Sending something is reading it.
        c->last_read_id = message->sf("id");
        c->mention_count = 0;
        return;
    }

    if (channel_id == g_open_channel)
    {
        c->last_read_id = message->sf("id");
        return;
    }

    // A direct message is addressed to whoever is in it, so every one counts.
    // In a server only a real mention does, which is the difference between a
    // number worth looking at and one that is on permanently.
    if (c->is_dm())
    {
        c->mention_count++;
        return;
    }

    const jval* mentions = message->arr("mentions");
    for (unsigned int i = 0; i < mentions->count; i++)
    {
        if (mentions->at(i)->sf("id") == self_id()) { c->mention_count++; return; }
    }

    if (message->boolean("mention_everyone", false)) c->mention_count++;
}

void store::mark_channel_read(snowflake channel_id)
{
    dchannel* c = find_channel(channel_id);
    if (!c) return;

    if (c->last_message_id) c->last_read_id = c->last_message_id;
    c->mention_count = 0;
    bump_revision();
}

// ---------------------------------------------------------------------------
// guild ordering
// ---------------------------------------------------------------------------

void store::apply_guild_order(const ulist<snowflake>* ids)
{
    if (!ids || !ids->count) return;

    ulist<snowflake> rebuilt;
    rebuilt = ulist<snowflake>();

    // Everything the account has an opinion about, in that order.
    for (unsigned int i = 0; i < ids->count; i++)
    {
        snowflake want = (*ids)[i];
        for (unsigned int k = 0; k < g_guild_order.count; k++)
        {
            if (g_guild_order[k] != want) continue;
            rebuilt.push(want);
            break;
        }
    }

    // Then anything it does not, which is where a freshly joined server sits
    // until the account is told about it.
    for (unsigned int k = 0; k < g_guild_order.count; k++)
    {
        bool placed = false;
        for (unsigned int i = 0; i < rebuilt.count && !placed; i++)
            if (rebuilt[i] == g_guild_order[k]) placed = true;
        if (!placed) rebuilt.push(g_guild_order[k]);
    }

    g_guild_order.dispose();
    g_guild_order = rebuilt;
    bump_revision();
}

void store::move_guild(snowflake id, int to_index)
{
    int from = -1;
    for (unsigned int i = 0; i < g_guild_order.count; i++)
        if (g_guild_order[i] == id) { from = (int)i; break; }

    if (from < 0) return;
    if (to_index < 0) to_index = 0;
    if (to_index >= (int)g_guild_order.count) to_index = (int)g_guild_order.count - 1;
    if (from == to_index) return;

    g_guild_order.delete_at((unsigned int)from);
    if (to_index > (int)g_guild_order.count) to_index = (int)g_guild_order.count;

    // Reinsert by shifting: the list is small enough that a memmove is not
    // worth a second code path.
    g_guild_order.push(0);
    for (int i = (int)g_guild_order.count - 1; i > to_index; i--)
        g_guild_order[i] = g_guild_order[i - 1];
    g_guild_order[to_index] = id;

    bump_revision();
}

store::guild_voice_summary store::voice_summary(snowflake guild_id)
{
    guild_voice_summary out;
    out.in_voice = 0;
    out.streaming = 0;

    const ulist<dvoice_state>& states = voice_states();
    for (unsigned int i = 0; i < states.count; i++)
    {
        if (states[i].guild_id != guild_id || !states[i].channel_id) continue;
        out.in_voice++;
        if (states[i].self_stream) out.streaming++;
    }
    return out;
}

// ---------------------------------------------------------------------------
// typing
// ---------------------------------------------------------------------------

namespace
{
    struct typing_note
    {
        snowflake channel_id;
        snowflake user_id;
        unsigned long long until_ms;
    };

    // Ten seconds is what discord's own clients assume, and a person who is
    // still typing sends another notice every few seconds anyway.
    const unsigned long long TYPING_LIFETIME_MS = 10000;
    const int TYPING_MAX = 32;

    typing_note g_typing[TYPING_MAX];
    int g_typing_count = 0;
}

void store::note_typing(snowflake channel_id, snowflake user_id)
{
    if (!channel_id || !user_id) return;
    if (user_id == self_id()) return;      // no point telling us about ourselves

    unsigned long long until = GetTickCount64() + TYPING_LIFETIME_MS;

    for (int i = 0; i < g_typing_count; i++)
    {
        if (g_typing[i].channel_id != channel_id || g_typing[i].user_id != user_id) continue;
        g_typing[i].until_ms = until;
        return;
    }

    if (g_typing_count >= TYPING_MAX)
    {
        // Drop whichever is closest to expiring rather than refusing the new
        // one: a full table means a busy channel, and the newest note is the
        // one worth keeping.
        int oldest = 0;
        for (int i = 1; i < g_typing_count; i++)
            if (g_typing[i].until_ms < g_typing[oldest].until_ms) oldest = i;

        g_typing[oldest] = g_typing[g_typing_count - 1];
        g_typing_count--;
    }

    g_typing[g_typing_count].channel_id = channel_id;
    g_typing[g_typing_count].user_id = user_id;
    g_typing[g_typing_count].until_ms = until;
    g_typing_count++;
}

int store::typing_in(snowflake channel_id, snowflake* out, int cap)
{
    unsigned long long now = GetTickCount64();
    int found = 0;

    for (int i = 0; i < g_typing_count; )
    {
        if (g_typing[i].until_ms <= now)
        {
            // Swept here rather than on a timer: this is the only place that
            // cares, and it runs every frame the chat is open.
            g_typing[i] = g_typing[g_typing_count - 1];
            g_typing_count--;
            continue;
        }

        if (g_typing[i].channel_id == channel_id && out && found < cap)
            out[found++] = g_typing[i].user_id;

        i++;
    }

    return found;
}
