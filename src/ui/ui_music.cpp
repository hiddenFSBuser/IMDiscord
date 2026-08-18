#include "pch.h"
#include "ui_state.h"
#include "theme.h"

#include "audio/music.h"
#include "discord/voice.h"
#include "system/io/ufile.h"

// The music player.
//
// Small on purpose: a track, a line to drag, two knobs. Anything more would be
// a media player, and what this is for is putting something on for the people
// in the channel.

namespace
{
    // Dragging the seek line has to hold its own position while the mouse is
    // down, or the track being read underneath would fight the hand.
    bool g_seeking = false;
    float g_seek_at = 0.0f;

    void clock(double seconds, char* out, int cap)
    {
        if (seconds < 0.0) seconds = 0.0;

        int total = (int)seconds;
        cnprint(out, cap, "%d:%02d", total / 60, total % 60);
    }
}

void ui_open_music()
{
    g_ui.open_music_popup = true;
}

void ui_view_music_popup()
{
    if (g_ui.open_music_popup)
    {
        ImGui::OpenPopup("##music");
        g_ui.open_music_popup = false;
    }

    ImGui::SetNextWindowSize(ImVec2(440, 260), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("##music", 0, ImGuiWindowFlags_NoTitleBar)) return;

    ImGui::TextUnformatted(tr("Музыка в голосовой канал"));
    ImGui::Separator();

    if (ImGui::Button(tr("Выбрать mp3"), ImVec2(150, 30)))
    {
        wchar_t chosen[MAX_PATH];
        if (ufile::open_dialog(chosen, MAX_PATH)) music::open(chosen);
    }

    if (music::loaded())
    {
        ImGui::SameLine();
        if (ImGui::Button(tr("Убрать"), ImVec2(110, 30))) music::close();
    }

    if (music::error()[0])
    {
        ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
        ImGui::TextWrapped("%s", tr(music::error()));
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0, 6));

    if (!music::loaded())
    {
        ui_text_muted(tr("Ничего не выбрано"));
        ImGui::Separator();

        if (ImGui::Button(tr("Закрыть"), ImVec2(120, 30)) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
        return;
    }

    ImGui::TextUnformatted(music::title());

    // Where the frames go. Said plainly rather than left to be discovered:
    // pressing play with no call up looks broken otherwise.
    if (voice::state() != VOICE_CONNECTED)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
        ImGui::TextWrapped("%s", tr("Играет только в голосовом канале - зайдите в него"));
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0, 4));

    // ---- transport -------------------------------------------------------
    bool playing = music::playing();

    if (ImGui::Button(playing ? tr("Пауза") : tr("Играть"), ImVec2(110, 30)))
        music::set_playing(!playing);

    ImGui::SameLine();
    if (ImGui::Button(tr("Стоп"), ImVec2(110, 30)))
    {
        music::set_playing(false);
        music::seek(0.0);
    }

    ImGui::SameLine();
    if (ImGui::Button("-15", ImVec2(60, 30))) music::seek(music::position() - 15.0);

    ImGui::SameLine();
    if (ImGui::Button("+15", ImVec2(60, 30))) music::seek(music::position() + 15.0);

    // ---- where it has got to ---------------------------------------------
    double length = music::duration();
    float at = g_seeking ? g_seek_at : (float)music::position();

    char left[16];
    char right[16];
    clock(at, left, sizeof(left));
    clock(length, right, sizeof(right));

    ImGui::SetNextItemWidth(-90.0f);
    if (ImGui::SliderFloat("##seek", &at, 0.0f, (float)(length > 0.0 ? length : 1.0), ""))
    {
        g_seeking = true;
        g_seek_at = at;
    }

    // Applied on release rather than while dragging: seeking rebuilds the
    // decoder's position and resets the resampler, and doing that on every
    // frame of a drag is a stutter for everybody listening.
    if (g_seeking && !ImGui::IsItemActive())
    {
        music::seek((double)g_seek_at);
        g_seeking = false;
    }

    ImGui::SameLine();
    char span[40];
    cnprint(span, sizeof(span), "%s / %s", left, right);
    ui_text_muted(span);

    ImGui::Dummy(ImVec2(0, 6));

    // ---- how loud, and who hears it --------------------------------------
    float vol = music::volume();
    ImGui::SetNextItemWidth(220);
    if (ImGui::SliderFloat(tr("Громкость музыки"), &vol, 0.0f, 1.0f, "%.2f"))
        music::set_volume(vol);

    bool monitor = music::monitoring();
    if (ImGui::Checkbox(tr("Слышать самому"), &monitor)) music::set_monitoring(monitor);

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(tr("Канал слышит трек в любом случае - это только про ваши колонки"));

    ImGui::Separator();
    if (ImGui::Button(tr("Закрыть"), ImVec2(120, 30)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}
