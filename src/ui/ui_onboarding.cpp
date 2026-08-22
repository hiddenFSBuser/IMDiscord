#include "pch.h"
#include "ui_state.h"
#include "theme.h"
#include "textures.h"
#include "core/app.h"

#include "discord/store.h"
#include "discord/rest.h"

// The panel a big server shows on the way in: pick your roles, then read the
// rules and agree to them.
//
// Two unrelated things that discord happens to show together, and this window
// keeps them together for the same reason - they are both "the things this
// server wants from you before you are properly in it". Either half can be
// missing: most servers have neither, some have only the rules.
//
// Nothing here is sent until the button at the bottom. Ticking a role is a
// change to a form, not a request, which is what lets a required prompt be
// enforced before anything leaves the client.

namespace
{
    const int MAX_PROMPTS = 32;
    const int MAX_OPTIONS = 256;

    api::onboard_prompt g_prompts[MAX_PROMPTS];
    api::onboard_option g_options[MAX_OPTIONS];
    int g_prompt_count = 0;
    int g_option_count = 0;

    // Which options are ticked, by index into g_options.
    bool g_picked[MAX_OPTIONS];

    bool g_agreed = false;
    bool g_sent = false;
    unsigned long long g_loaded_at = 0;

    void reload()
    {
        g_prompt_count = api::onboarding_prompts(g_prompts, MAX_PROMPTS);
        g_option_count = api::onboarding_options(g_options, MAX_OPTIONS);
    }

    // A prompt the panel is meant to show. The others belong to the channel
    // list, where a server keeps its role pickers for later.
    bool shown(const api::onboard_prompt* p)
    {
        return p->in_onboarding && p->option_count > 0;
    }

    bool prompt_answered(const api::onboard_prompt* p)
    {
        for (int k = 0; k < p->option_count; k++)
            if (g_picked[p->first_option + k]) return true;
        return false;
    }

    void pick(const api::onboard_prompt* p, int index)
    {
        // A single-choice prompt is a set of buttons where one is in: ticking
        // one unticks the rest rather than refusing the click, which is what
        // makes changing your mind one action instead of two.
        if (p->single_select && !g_picked[index])
            for (int k = 0; k < p->option_count; k++) g_picked[p->first_option + k] = false;

        g_picked[index] = !g_picked[index];
    }

    void draw_option(const api::onboard_prompt* p, int index, float width)
    {
        const api::onboard_option* o = &g_options[index];

        bool has_desc = o->description[0] != 0;
        float row_h = has_desc ? 46.0f : 30.0f;

        ImGui::PushID((const void*)(size_t)o->id);

        ImVec2 start = ImGui::GetCursorScreenPos();
        bool clicked = ImGui::InvisibleButton("##opt", ImVec2(width, row_h));
        bool hovered = ImGui::IsItemHovered();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 bg = g_picked[index] ? col::bg_active : (hovered ? col::bg_hover : col::bg_panel);
        dl->AddRectFilled(start, ImVec2(start.x + width, start.y + row_h), bg, 6.0f);

        if (g_picked[index])
            dl->AddRect(start, ImVec2(start.x + width, start.y + row_h), col::accent, 6.0f, 0, 2.0f);

        float x = start.x + 10.0f;
        float line = ImGui::GetTextLineHeight();

        // A custom emoji is a picture on discord's cdn; a plain one is the
        // characters themselves, which the shared text drawing already knows
        // how to turn into a picture when that is switched on.
        if (o->emoji_id)
        {
            char url[160];
            cdn::custom_emoji(o->emoji_id, o->emoji_animated, 48, url, sizeof(url));

            const texture* t = tex::get(url);
            if (t->ready())
                dl->AddImage(t->id(), ImVec2(x, start.y + 7.0f),
                             ImVec2(x + line, start.y + 7.0f + line));

            x += line + 6.0f;
        }

        char label[192];
        if (!o->emoji_id && o->emoji_name[0])
            cnprint(label, sizeof(label), "%s %s", o->emoji_name, o->title);
        else
            cnprint(label, sizeof(label), "%s", o->title);

        x += ui_draw_text_emoji(dl, ImVec2(x, start.y + 7.0f), col::text_normal, label);

        if (has_desc)
            dl->AddText(ImVec2(start.x + 10.0f, start.y + 26.0f), col::text_muted, o->description);

        // The roles it grants, on the right. What a pick actually does is the
        // one thing the server's own wording tends to leave out.
        if (o->role_count)
        {
            store::guard guard;
            dguild* g = store::find_guild(g_ui.onboard_guild);

            char roles[160];
            roles[0] = 0;

            for (int r = 0; r < o->role_count && g; r++)
            {
                const drole* role = store::find_role(g, o->roles[r]);
                if (!role || !role->name) continue;

                int at = (int)ccslenf(roles);
                if (at) cnprint(roles + at, (int)sizeof(roles) - at, ", %s", role->name);
                else    cnprint(roles, sizeof(roles), "%s", role->name);
            }

            if (roles[0])
            {
                float w = ImGui::CalcTextSize(roles).x;
                if (w < width * 0.4f)
                    dl->AddText(ImVec2(start.x + width - w - 10.0f, start.y + 7.0f),
                                col::text_muted, roles);
            }
        }

        if (clicked) pick(p, index);
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, 4));
    }
}

void ui_open_onboarding(snowflake guild_id)
{
    g_ui.onboard_guild = guild_id;
    g_ui.open_onboard_popup = true;

    ccfset(g_picked, 0, sizeof(g_picked));
    g_agreed = false;
    g_sent = false;
    g_prompt_count = 0;
    g_option_count = 0;
    g_loaded_at = 0;

    api::clear_last_error();
    api::fetch_onboarding(guild_id);
    api::fetch_rules(guild_id);
}

void ui_view_onboarding_popup()
{
    if (g_ui.open_onboard_popup)
    {
        ImGui::OpenPopup("##onboarding");
        g_ui.open_onboard_popup = false;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                   vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620, 620), ImGuiCond_Appearing);

    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(col::bg_panel));
    if (!ImGui::BeginPopupModal("##onboarding", 0, ImGuiWindowFlags_NoTitleBar))
    {
        ImGui::PopStyleColor();
        return;
    }

    // Read once the fetches have settled rather than every frame: copying two
    // hundred options out from under a lock is not free, and they do not change
    // while the window is open.
    if (!api::onboarding_loading() && !g_loaded_at)
    {
        g_loaded_at = GetTickCount64();
        reload();
    }

    {
        store::guard guard;
        dguild* g = store::find_guild(g_ui.onboard_guild);

        ImGui::PushFont(g_app.font_big);
        ImGui::TextUnformatted(g && g->name ? g->name : tr("Сервер"));
        ImGui::PopFont();
    }

    char desc[512];
    api::rules_description(desc, sizeof(desc));
    if (desc[0]) ui_text_muted(desc);

    ImGui::Separator();

    if (api::onboarding_loading())
    {
        ImGui::Dummy(ImVec2(0, 10));
        ui_text_muted(tr("Загружаю..."));
    }

    int rules = api::rules_count();
    bool anything = g_prompt_count > 0 || rules > 0;

    if (!api::onboarding_loading() && !anything)
    {
        ImGui::Dummy(ImVec2(0, 10));
        ui_text_muted(tr("Этот сервер ничего не спрашивает при входе."));
    }

    ImGui::BeginChild("##onbscroll", ImVec2(0, 440.0f), false);

    float width = ImGui::GetContentRegionAvail().x - 6.0f;

    for (int i = 0; i < g_prompt_count; i++)
    {
        api::onboard_prompt* p = &g_prompts[i];
        if (!shown(p)) continue;

        ImGui::Dummy(ImVec2(0, 6));

        ImGui::PushFont(g_app.font_bold);
        ImGui::TextUnformatted(p->title);
        ImGui::PopFont();

        // Said before the options rather than as an error afterwards.
        if (p->required && !prompt_answered(p))
        {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
            ImGui::TextUnformatted(tr("- нужно выбрать"));
            ImGui::PopStyleColor();
        }
        else if (p->single_select)
        {
            ImGui::SameLine();
            ui_text_muted(tr("- один вариант"));
        }

        ImGui::Dummy(ImVec2(0, 2));

        for (int k = 0; k < p->option_count; k++)
            draw_option(p, p->first_option + k, width);
    }

    if (rules)
    {
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::Separator();

        ImGui::PushFont(g_app.font_bold);
        ImGui::TextUnformatted(tr("Правила сервера"));
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 4));

        for (int i = 0; i < rules; i++)
        {
            char line[1024];
            api::rules_line(i, line, sizeof(line));
            if (!line[0]) continue;

            char numbered[1100];
            cnprint(numbered, sizeof(numbered), "%d. %s", i + 1, line);

            ImGui::PushTextWrapPos(width);
            ImGui::TextUnformatted(numbered);
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(0, 3));
        }

        ImGui::Dummy(ImVec2(0, 6));
        ImGui::Checkbox(tr("Прочитал и согласен с правилами"), &g_agreed);
    }

    ImGui::EndChild();

    ImGui::Separator();

    // Everything a required prompt asks for, before anything is sent. Discord
    // would refuse it anyway; refusing here means saying which one.
    bool missing = false;
    for (int i = 0; i < g_prompt_count; i++)
        if (shown(&g_prompts[i]) && g_prompts[i].required && !prompt_answered(&g_prompts[i]))
            missing = true;

    bool blocked = missing || (rules > 0 && !g_agreed) || g_sent;

    if (blocked) ImGui::BeginDisabled();

    if (ImGui::Button(tr("Готово"), ImVec2(160, 32)))
    {
        snowflake chosen[64];
        int count = 0;

        for (int i = 0; i < g_option_count && count < 64; i++)
            if (g_picked[i]) chosen[count++] = g_options[i].id;

        // The rules first: a server with a gate does not let the roles through
        // until they are agreed to, so the other order sends a choice that is
        // thrown away.
        if (rules > 0) api::accept_rules(g_ui.onboard_guild);
        if (g_prompt_count > 0) api::submit_onboarding(g_ui.onboard_guild, chosen, count);

        g_sent = true;
    }

    if (blocked) ImGui::EndDisabled();

    if (missing)
    {
        ImGui::SameLine();
        ui_text_muted(tr("не всё выбрано"));
    }
    else if (rules > 0 && !g_agreed)
    {
        ImGui::SameLine();
        ui_text_muted(tr("нужно согласиться с правилами"));
    }

    ImGui::SameLine();
    if (ImGui::Button(tr("Закрыть"), ImVec2(140, 32)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        api::clear_last_error();
        ImGui::CloseCurrentPopup();
    }

    if (api::last_error()[0])
    {
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(api::last_error());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }

    ImGui::EndPopup();
    ImGui::PopStyleColor();
}
