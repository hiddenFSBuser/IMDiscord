#include "pch.h"
#include "ui_state.h"
#include "theme.h"

#include "discord/store.h"
#include "discord/rest.h"
#include "discord/science.h"

// Handing the server to somebody else.
//
// Two steps, and the order is discord's rather than ours: it mails a six digit
// code to the owner's address and refuses to move the crown without it. This
// client cannot read that mail and should not try to - the code exists so that
// somebody who has taken over a session cannot give away a server on the way
// out, and typing it in by hand is the whole of what makes it work.
//
// Its own window rather than a line in the server settings, because it is the
// one action here that cannot be undone from inside this client: afterwards
// the crown is somebody else's, and only they can give it back.

void ui_open_ownership(snowflake guild_id, snowflake user_id)
{
    g_ui.ownership_guild = guild_id;
    g_ui.ownership_user = user_id;
    g_ui.open_ownership_popup = true;

    ccfset(g_ui.ownership_code, 0, sizeof(g_ui.ownership_code));

    api::clear_ownership_state();
    api::clear_last_error();

    science::transfer_ownership_opened(guild_id);
}

void ui_view_ownership_popup()
{
    if (g_ui.open_ownership_popup)
    {
        ImGui::OpenPopup("##ownership");
        g_ui.open_ownership_popup = false;
    }

    ImGui::SetNextWindowSize(ImVec2(460, 300), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("##ownership", 0, ImGuiWindowFlags_NoTitleBar)) return;

    store::guard guard;

    dguild* g = store::find_guild(g_ui.ownership_guild);
    duser* u = store::find_user(g_ui.ownership_user);

    if (!g || !u)
    {
        ui_text_muted(tr("Сервер недоступен"));
        if (ImGui::Button(tr("Закрыть"), ImVec2(120, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::TextUnformatted(tr("Передача владения сервером"));
    ImGui::Separator();

    char line[256];
    cnprint(line, sizeof(line), tr("%s станет владельцем «%s»"),
            u->display_name(), g->name ? g->name : "");

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(line);
    ImGui::PopTextWrapPos();

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(tr("Это необратимо. Вернуть корону сможет только новый владелец."));
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();

    // Offered anyway - the refusal, if there is one, comes from discord and
    // shows up below like any other error.
    if (u->bot)
    {
        ImGui::Dummy(ImVec2(0, 4));
        ui_text_muted(tr("Это бот - discord может отказать."));
    }

    ImGui::Dummy(ImVec2(0, 10));

    // ---- step one: the code ----------------------------------------------
    if (ImGui::Button(api::ownership_code_sent() ? tr("Отправить код заново")
                                                 : tr("Отправить код на почту"),
                      ImVec2(240, 30)))
        api::request_ownership_code(g->id);

    // Each request replaces the last one, and discord answers a stale code with
    // the same "incorrect code" it gives a mistyped one - which is a confusing
    // thing to be told while a letter with a code in it is open on the screen.
    // So the age is on show, and an old one is called old.
    if (api::ownership_code_sent())
    {
        unsigned long long age = api::ownership_code_age_ms() / 60000ULL;

        char when[96];
        if (!age) cnprint(when, sizeof(when), tr("Код отправлен только что."));
        else      cnprint(when, sizeof(when), tr("Код отправлен %llu мин назад."), age);

        if (age >= 10)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
            ImGui::TextUnformatted(when);
            ImGui::TextUnformatted(tr("Мог протухнуть - запросите новый."));
            ImGui::PopStyleColor();
        }
        else
        {
            ui_text_muted(when);
            ui_text_muted(tr("Считается только последнее письмо."));
        }
    }

    ImGui::Dummy(ImVec2(0, 8));

    // ---- step two: the code, and the crown --------------------------------
    ImGui::TextUnformatted(tr("Код из письма"));
    ImGui::SetNextItemWidth(160);

    // Letters as well as digits - the mail sends something like "A3F9K2QB",
    // and a digits-only field silently ate half of it. Only spaces are kept
    // out, since the code never has one and pasting from the mail drags them
    // along.
    ImGui::InputText("##pincode", g_ui.ownership_code, sizeof(g_ui.ownership_code),
                     ImGuiInputTextFlags_CharsNoBlank);

    // The count, because a wrong code and a code that lost a character to a
    // clumsy paste are told apart by nothing else: discord's answer is the
    // same sentence for both.
    int typed = 0;
    while (g_ui.ownership_code[typed]) typed++;

    if (typed)
    {
        char count[64];
        cnprint(count, sizeof(count), tr("%d символов"), typed);
        ImGui::SameLine();
        ui_text_muted(count);
    }

    ImGui::Dummy(ImVec2(0, 6));

    // Two conditions, and the second is the one that matters: the code has to
    // have been asked for from this window. Otherwise the code being typed is
    // whatever old letter happens to be open, and an old code is refused in
    // exactly the same words as a wrong one - which is how an evening goes
    // into re-reading a request that was correct all along.
    bool ready = g_ui.ownership_code[0] != 0 && api::ownership_code_sent();

    if (!ready) ImGui::BeginDisabled();

    ImGui::PushStyleColor(ImGuiCol_Button, col::red);
    if (ImGui::Button(tr("Передать сервер"), ImVec2(200, 32)))
        api::transfer_ownership(g->id, u->id, g_ui.ownership_code);
    ImGui::PopStyleColor();

    if (!ready) ImGui::EndDisabled();

    if (!api::ownership_code_sent())
    {
        ImGui::SameLine();
        ui_text_muted(tr("сначала запросите код"));
    }

    if (api::last_error()[0])
    {
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
        ImGui::TextWrapped("%s", api::last_error());
        ImGui::PopStyleColor();
    }

    ImGui::Separator();
    if (ImGui::Button(tr("Закрыть"), ImVec2(120, 30)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        api::clear_last_error();
        ccfset(g_ui.ownership_code, 0, sizeof(g_ui.ownership_code));
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}
