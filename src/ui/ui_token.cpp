#include "pch.h"
#include "ui_state.h"
#include "theme.h"
#include "core/app.h"
#include "core/storage.h"
#include "core/crypto.h"

// Showing an account's token, behind a small test that has to be done by hand.
//
// What this is for, said plainly, because the alternative is somebody trusting
// it further than it goes: a token is the account. Anything holding it is
// signed in as you, for as long as the password is unchanged. The reason it is
// behind anything at all is that remote access tools are pointed at whatever is
// already on the screen, and the cheap ones drive a client by clicking known
// places and reading known text. A value that appears only after somebody
// aligns three sliders they cannot see the targets of in advance is a value
// those tools do not walk away with by accident.
//
// It is not protection against somebody who has run code on this machine. They
// can read the file the tokens are kept in, or the memory of this process, and
// this window makes no difference to either. It is a speed bump against being
// watched, not a lock.

namespace
{
    const int SLIDERS = 3;

    // How close counts as aligned, as a fraction of the track. Small enough
    // that dragging roughly there is not enough, wide enough that it can be
    // done with a hand rather than a steady hand.
    const float TOLERANCE = 0.025f;

    // Held in place for this long before it counts. A single frame in the right
    // spot is something a passing swipe does; a second of it is not.
    const unsigned long long HOLD_MS = 700;

    // After this the puzzle is thrown away and set again. Somebody working out
    // the answer slowly, from somewhere else, does not get to keep it.
    const unsigned long long LIFE_MS = 10000;

    float g_target[SLIDERS];
    float g_value[SLIDERS];

    unsigned long long g_set_at = 0;
    unsigned long long g_aligned_since = 0;

    // Once done, done for a while. Twelve hours, kept on disk so it survives
    // the client being restarted - which is the point: a test that has to be
    // passed again every time somebody wants to look at a token is a test
    // that stops being done and starts being resented.
    //
    // Written as the second the test was passed, in unix time, because ticks
    // since boot mean nothing across a restart.
    const int ACCESS_HOURS = 12;

    int access_granted_at()
    {
        return storage::settings_get_int("token_access_at", 0);
    }

    // Seconds of access left, or zero.
    long long access_left()
    {
        int at = access_granted_at();
        if (!at) return 0;

        long long now = (long long)(unix_now_ms() / 1000ULL);
        long long ends = (long long)at + ACCESS_HOURS * 3600;

        // A clock moved backwards would otherwise hand out access forever.
        if (now < (long long)at) return 0;
        return now < ends ? ends - now : 0;
    }

    void grant_access()
    {
        storage::settings_set_int("token_access_at", (int)(unix_now_ms() / 1000ULL));
        storage::settings_save();
    }

    void revoke_access()
    {
        storage::settings_set_int("token_access_at", 0);
        storage::settings_save();
    }

    float random_unit()
    {
        unsigned char b[2];
        crypto::random_bytes(b, sizeof(b));

        unsigned int v = ((unsigned int)b[0] << 8) | b[1];
        return (float)v / 65535.0f;
    }

    void shuffle()
    {
        for (int i = 0; i < SLIDERS; i++)
        {
            // Kept off the ends, where a handle dragged as far as it goes would
            // land without aiming.
            g_target[i] = 0.12f + random_unit() * 0.76f;

            // And the handle never starts on the answer.
            do { g_value[i] = random_unit(); }
            while (g_value[i] > g_target[i] - 0.15f && g_value[i] < g_target[i] + 0.15f);
        }

        g_set_at = GetTickCount64();
        g_aligned_since = 0;
    }

    // One track with a notch to hit and a handle to drag. Drawn by hand rather
    // than with a slider widget because a slider shows its value, and a number
    // on the screen is the one thing this is trying not to give away.
    bool track(int index, float width)
    {
        const float H = 26.0f;
        const float KNOB = 11.0f;

        ImGui::PushID(index);

        ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##track", ImVec2(width, H));

        bool held = ImGui::IsItemActive();
        float left = at.x + KNOB;
        float span = width - KNOB * 2.0f;

        if (held && span > 1.0f)
        {
            float x = ImGui::GetIO().MousePos.x - left;
            float v = x / span;

            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            g_value[index] = v;
        }

        bool ok = g_value[index] > g_target[index] - TOLERANCE &&
                  g_value[index] < g_target[index] + TOLERANCE;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        float mid = at.y + H * 0.5f;

        dl->AddRectFilled(ImVec2(at.x, mid - 3.0f), ImVec2(at.x + width, mid + 3.0f),
                          col::bg_input, 3.0f);

        // The notch to hit. Drawn as a gap in the bar rather than as a number.
        float tx = left + g_target[index] * span;
        dl->AddRectFilled(ImVec2(tx - 2.0f, mid - 9.0f), ImVec2(tx + 2.0f, mid + 9.0f),
                          ok ? col::green : col::text_muted, 1.0f);

        float hx = left + g_value[index] * span;
        dl->AddCircleFilled(ImVec2(hx, mid), KNOB, ok ? col::green : col::accent);

        if (ImGui::IsItemHovered() || held) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        ImGui::PopID();
        return ok;
    }
}

void ui_open_token(int account_index)
{
    g_ui.token_account = account_index;
    g_ui.open_token_popup = true;

    // Only set up the puzzle when there is one to do.
    if (!access_left()) shuffle();
}

void ui_view_token_popup()
{
    if (g_ui.open_token_popup)
    {
        ImGui::OpenPopup("##token");
        g_ui.open_token_popup = false;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                   vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460, 0));

    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(col::bg_panel));
    if (!ImGui::BeginPopupModal("##token", 0,
                                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PopStyleColor();
        return;
    }

    const saved_account* entry = storage::account_at(g_ui.token_account);

    if (!entry)
    {
        ui_text_muted(tr("Аккаунт недоступен"));
        if (ImGui::Button(tr("Закрыть"), ImVec2(120, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        ImGui::PopStyleColor();
        return;
    }

    ImGui::TextUnformatted(tr("Токен аккаунта"));
    ImGui::SameLine();
    ui_text_muted(entry->name[0] ? entry->name : tr("Аккаунт"));
    ImGui::Separator();

    unsigned long long now = GetTickCount64();
    long long left = access_left();

    if (!left)
    {
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(tr("Совместите каждый ползунок с меткой и удержите."));
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 8));

        bool all = true;
        for (int i = 0; i < SLIDERS; i++)
        {
            if (!track(i, 420.0f)) all = false;
            ImGui::Dummy(ImVec2(0, 6));
        }

        if (all)
        {
            if (!g_aligned_since) g_aligned_since = now;
            if (now - g_aligned_since >= HOLD_MS) grant_access();
        }
        else
        {
            g_aligned_since = 0;
        }

        // Set again from scratch when the time runs out. Said out loud, so a
        // puzzle that changes under somebody's hands does not read as a fault.
        if (now - g_set_at > LIFE_MS)
        {
            shuffle();
            ui_text_muted(tr("Время вышло - новые метки"));
        }
        else if (all)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, col::green);
            ImGui::TextUnformatted(tr("Держите..."));
            ImGui::PopStyleColor();
        }
        else
        {
            unsigned long long secs = (LIFE_MS - (now - g_set_at)) / 1000;

            char line[64];
            cnprint(line, sizeof(line), tr("осталось %llu с"), secs);
            ui_text_muted(line);
        }
    }
    else
    {
        ImGui::Dummy(ImVec2(0, 4));

        ImGui::PushStyleColor(ImGuiCol_Text, col::yellow);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(tr("Это и есть доступ к аккаунту. Кто его получит - войдёт "
                                  "как вы, пока не сменён пароль."));
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, 6));

        // Read only, and selectable, so it can be picked out by hand as well
        // as copied whole.
        char shown[512];
        ccstrncpy(shown, entry->token, sizeof(shown) - 1);
        shown[sizeof(shown) - 1] = 0;

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##tok", shown, sizeof(shown), ImGuiInputTextFlags_ReadOnly);

        ImGui::Dummy(ImVec2(0, 4));

        if (ImGui::Button(tr("Скопировать"), ImVec2(160, 30)))
            ImGui::SetClipboardText(entry->token);

        ImGui::SameLine();
        if (ImGui::Button(tr("Закрыть доступ"), ImVec2(160, 30)))
        {
            revoke_access();
            shuffle();
        }

        char line[96];
        if (left > 3600) cnprint(line, sizeof(line), tr("Без проверки ещё %lld ч"), left / 3600);
        else             cnprint(line, sizeof(line), tr("Без проверки ещё %lld мин"), left / 60);
        ui_text_muted(line);
    }

    ImGui::Separator();
    if (ImGui::Button(tr("Закрыть"), ImVec2(120, 30)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
    ImGui::PopStyleColor();
}
