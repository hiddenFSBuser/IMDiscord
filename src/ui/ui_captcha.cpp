#include "pch.h"
#include "ui_state.h"
#include "theme.h"
#include "core/app.h"
#include "discord/captcha.h"

// While a captcha is being solved in the default browser: what is waiting,
// where to solve it, and a way out. A plain window rather than a popup -
// it lives exactly as long as the solve does, with no open/close state to
// keep in step.
void ui_view_captcha_popup()
{
    if (!captcha::waiting()) return;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                   vp->WorkPos.y + vp->WorkSize.y * 0.4f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460, 0));

    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ImGui::ColorConvertU32ToFloat4(col::bg_panel));
    if (!ImGui::Begin("##captcha", 0,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                      ImGuiWindowFlags_AlwaysAutoResize |
                      ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::PopStyleColor();
        return;
    }

    ImGui::PushFont(g_app.font_bold);
    ImGui::TextUnformatted(tr("Discord требует капчу"));
    ImGui::PopFont();

    ImGui::SameLine(0, 8);
    ui_text_muted(captcha::action_text());

    ImGui::Separator();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(tr("Открой страницу в браузере и реши её — токен подхватится сам."));
    ImGui::PopTextWrapPos();

    ImGui::Dummy(ImVec2(0, 4));
    ui_text_muted(tr("Браузер ходит напрямую, без прокси."));

    ImGui::Dummy(ImVec2(0, 4));
    bool auto_open = captcha::auto_open();
    if (ImGui::Checkbox(tr("Сразу открывать браузер с капчой"), &auto_open))
        captcha::set_auto_open(auto_open);

    ImGui::Dummy(ImVec2(0, 6));
    if (ImGui::Button(tr("Открыть в браузере"), ImVec2(200, 30)))
        captcha::reopen_browser();
    ImGui::SameLine();
    if (ImGui::Button(tr("Отмена"), ImVec2(120, 30)))
        captcha::cancel();

    ImGui::End();
    ImGui::PopStyleColor();
}
