#include "pch.h"
#include "ui_state.h"
#include "theme.h"

#include "discord/store.h"
#include "discord/rest.h"

// Making a channel or a category.
//
// Nothing is written into the store: CHANNEL_CREATE arrives on the gateway and
// is already handled there, so a channel appears in the list the same way one
// made by somebody else does.

namespace
{
    // Discord's own numbering.
    const int TYPE_TEXT = 0;
    const int TYPE_VOICE = 2;
    const int TYPE_CATEGORY = 4;

    int g_kind = TYPE_TEXT;
    char g_name[96];
    snowflake g_parent = 0;

    void parent_picker(dguild* g)
    {
        dchannel* current = store::find_channel(g_parent);
        const char* label = current && current->name ? current->name : tr("без категории");

        // The label doubles as the widget's identity, and the radio button
        // above says "Категория" too - two items claiming one id, which imgui
        // reports as a conflict and then draws wrongly. The part after ## is
        // identity only and is not shown.
        ImGui::SetNextItemWidth(300);
        if (!ImGui::BeginCombo(tr("Категория##parent"), label)) return;

        if (ImGui::Selectable(tr("без категории"), g_parent == 0)) g_parent = 0;

        for (unsigned int i = 0; i < g->channels.count; i++)
        {
            dchannel* c = store::find_channel(g->channels[i]);
            if (!c || c->type != CH_CATEGORY) continue;

            ImGui::PushID((const void*)(size_t)c->id);
            if (ImGui::Selectable(c->name ? c->name : tr("без имени"), c->id == g_parent))
                g_parent = c->id;
            ImGui::PopID();
        }

        ImGui::EndCombo();
    }
}

void ui_open_new_channel(snowflake guild_id)
{
    g_ui.newchan_guild = guild_id;
    g_ui.open_newchan_popup = true;

    g_kind = TYPE_TEXT;
    g_parent = 0;
    ccfset(g_name, 0, sizeof(g_name));

    api::clear_last_error();
}

void ui_view_new_channel_popup()
{
    if (g_ui.open_newchan_popup)
    {
        ImGui::OpenPopup("##newchan");
        g_ui.open_newchan_popup = false;
    }

    ImGui::SetNextWindowSize(ImVec2(460, 300), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("##newchan", 0, ImGuiWindowFlags_NoTitleBar)) return;

    store::guard guard;
    dguild* g = store::find_guild(g_ui.newchan_guild);

    if (!g)
    {
        ui_text_muted(tr("Сервер недоступен"));
        if (ImGui::Button(tr("Закрыть"), ImVec2(120, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::TextUnformatted(tr("Новый канал"));
    ImGui::SameLine();
    ui_text_muted(g->name ? g->name : "");
    ImGui::Separator();

    ImGui::RadioButton(tr("Текстовый"), &g_kind, TYPE_TEXT);
    ImGui::SameLine();
    ImGui::RadioButton(tr("Голосовой"), &g_kind, TYPE_VOICE);
    ImGui::SameLine();
    ImGui::RadioButton(tr("Категория"), &g_kind, TYPE_CATEGORY);

    ImGui::Dummy(ImVec2(0, 6));
    ImGui::SetNextItemWidth(300);
    ImGui::InputText(tr("Название"), g_name, sizeof(g_name));

    // A category has nowhere to be put, so the choice is not offered for one
    // rather than offered and ignored.
    if (g_kind == TYPE_CATEGORY)
        ui_text_muted(tr("Категория не может находиться внутри другой"));
    else
        parent_picker(g);

    if (g_kind == TYPE_TEXT)
        ui_text_muted(tr("Пробелы в названии Discord заменит на дефисы"));

    ImGui::Dummy(ImVec2(0, 10));

    if (ImGui::Button(tr("Создать"), ImVec2(130, 30)) && g_name[0])
    {
        api::clear_last_error();
        api::create_channel(g->id, g_name, g_kind, g_parent);
        ccfset(g_name, 0, sizeof(g_name));
    }

    ImGui::SameLine();
    if (ImGui::Button(tr("Закрыть"), ImVec2(130, 30)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        api::clear_last_error();
        ImGui::CloseCurrentPopup();
    }

    if (api::last_error()[0])
    {
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
        ImGui::TextWrapped("%s", api::last_error());
        ImGui::PopStyleColor();
    }

    ImGui::EndPopup();
}
