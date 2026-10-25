// ui/fear_pane.cpp -- see ui/fear_pane.h.

#include "ui/fear_pane.h"

#include "coop/config/config.h"
#include "coop/config/config_registry.h"
#include "coop/fear/fear_catalog.h"
#include "coop/fear/fear_director.h"
#include "l10n/l10n.h"
#include "ui/scale.h"

#include "imgui.h"

#include <cstdio>
#include <string>

namespace ui::fear_pane {
namespace {

using ui::scale::S;
namespace CR = coop::config_registry;
namespace CFG = coop::config;
namespace D = coop::fear;

// Render thread only: the text boxes' contents between frames.
char g_key[256] = {};
char g_model[96] = {};
char g_language[48] = {};
bool g_boxesPrimed = false;

void SetFlag(const CR::FlagRow& row, bool v) { CFG::SetValue(row, v ? "1" : "0"); }

void FlagBox(const char* label, const char* id, const CR::FlagRow& row) {
    bool v = CFG::ResolveFlag(row);
    if (ImGui::Checkbox(l10n::Label(label, id), &v)) SetFlag(row, v);
}

void PrimeBoxes() {
    if (g_boxesPrimed) return;
    g_boxesPrimed = true;
    std::snprintf(g_model, sizeof(g_model), "%s", CFG::ResolveString(CR::rows::director_ai_model).c_str());
    std::snprintf(g_language, sizeof(g_language), "%s", CFG::ResolveString(CR::rows::director_ai_language).c_str());
}

void RenderStatus(const D::View& v) {
    char line[256];
    std::snprintf(line, sizeof(line), "%.0f / %.0f", v.tension, v.threshold);
    ImGui::TextUnformatted(l10n::T("Tension"));
    ImGui::SameLine(S(120.f));
    ImGui::ProgressBar(v.tension / 100.f, ImVec2(-1.f, 0.f), line);
    if (v.hold[0]) {
        l10n::Fmt(line, sizeof(line), l10n::T("Holding: %s"), v.hold);
        ImGui::TextDisabled("%s", line);
    } else if (v.relaxLeftS > 0) {
        l10n::Fmt(line, sizeof(line), l10n::T("Letting the players breathe: %d s"), v.relaxLeftS);
        ImGui::TextDisabled("%s", line);
    } else {
        ImGui::TextDisabled("%s", l10n::T("Watching the players."));
    }
    if (v.aiOn) {
        l10n::Fmt(line, sizeof(line), l10n::T("AI agent: %s"), v.aiState[0] ? v.aiState : l10n::T("waiting for its first look"));
        ImGui::TextWrapped("%s", line);
        if (v.aiNote[0]) ImGui::TextDisabled("\"%s\"", v.aiNote);
    }
}

void RenderSettings() {
    FlagBox(l10n::T("Director on"), "director_on", CR::rows::director_enabled);
    int intensity = static_cast<int>(CFG::ResolveInt(CR::rows::director_intensity));
    ImGui::SetNextItemWidth(S(220.f));
    if (ImGui::SliderInt(l10n::Label(l10n::T("Intensity"), "director_intensity"), &intensity, 1, 3)) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%d", intensity);
        CFG::SetValue(CR::rows::director_intensity, buf);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", intensity <= 1 ? l10n::T("slow burn") : intensity == 2 ? l10n::T("normal")
                                                                                    : l10n::T("relentless"));
    FlagBox(l10n::T("Hold scares while anyone sleeps"), "director_sleep", CR::rows::director_respect_sleep);
    FlagBox(l10n::T("Allow the deadly invasions"), "director_dangerous", CR::rows::director_allow_dangerous);
}

void RenderAi() {
    PrimeBoxes();
    FlagBox(l10n::T("AI agent chooses the scares"), "director_ai", CR::rows::director_ai);
    const bool haveKey = !CFG::ResolveString(CR::rows::director_ai_key).empty();
    ImGui::TextDisabled("%s", haveKey ? l10n::T("API key: set (kept on this computer only)")
                                      : l10n::T("API key: not set -- get one at console.anthropic.com"));
    ImGui::SetNextItemWidth(S(320.f));
    ImGui::InputText("##director_key", g_key, sizeof(g_key), ImGuiInputTextFlags_Password);
    ImGui::SameLine();
    if (ImGui::Button(l10n::Label(l10n::T("Save key"), "director_key_save")) && g_key[0]) {
        CFG::SetValue(CR::rows::director_ai_key, g_key);
        g_key[0] = '\0';
    }
    if (haveKey) {
        ImGui::SameLine();
        if (ImGui::Button(l10n::Label(l10n::T("Forget key"), "director_key_forget"))) CFG::ResetValue(CR::rows::director_ai_key);
    }
    ImGui::SetNextItemWidth(S(220.f));
    if (ImGui::InputText(l10n::Label(l10n::T("Model"), "director_model"), g_model, sizeof(g_model),
                         ImGuiInputTextFlags_EnterReturnsTrue) && g_model[0])
        CFG::SetValue(CR::rows::director_ai_model, g_model);
    ImGui::SetNextItemWidth(S(220.f));
    if (ImGui::InputText(l10n::Label(l10n::T("Email language"), "director_language"), g_language,
                         sizeof(g_language), ImGuiInputTextFlags_EnterReturnsTrue) && g_language[0])
        CFG::SetValue(CR::rows::director_ai_language, g_language);
    FlagBox(l10n::T("The agent may send emails to the base"), "director_emails", CR::rows::director_ai_emails);
    int period = static_cast<int>(CFG::ResolveInt(CR::rows::director_ai_period_s));
    ImGui::SetNextItemWidth(S(220.f));
    if (ImGui::SliderInt(l10n::Label(l10n::T("Seconds between looks"), "director_period"), &period, 20, 600)) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%d", period);
        CFG::SetValue(CR::rows::director_ai_period_s, buf);
    }
}

void RenderTests() {
    if (ImGui::Button(l10n::Label(l10n::T("Scare now (the director picks)"), "director_pick"))) D::RequestPick();
    ImGui::SameLine();
    if (ImGui::Button(l10n::Label(l10n::T("Calm down"), "director_calm"))) D::RequestCalm();
    if (!ImGui::TreeNode(l10n::Label(l10n::T("Test one scare (both players should see it)"), "director_tests"))) return;
    size_t count = 0;
    const D::Scare* all = D::Catalog(&count);
    for (size_t i = 0; i < count; ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton(all[i].key)) D::RequestFire(all[i].key);
        ImGui::SameLine();
        ImGui::TextDisabled("%s%s -- %s", D::FlavorName(all[i].flavor), all[i].dangerous ? " (deadly)" : "",
                            all[i].what);
        ImGui::PopID();
    }
    ImGui::TreePop();
}

}  // namespace

void Render() {
    D::View v;
    D::ReadView(v);
    if (!v.hosting) {
        ImGui::TextWrapped("%s", l10n::T("The fear director runs on the host of a multiplayer session. Host one "
                                         "to use it; as a client you just get scared."));
        return;
    }
    RenderStatus(v);
    ImGui::Separator();
    RenderSettings();
    ImGui::Separator();
    if (ImGui::CollapsingHeader(l10n::Label(l10n::T("AI agent"), "director_ai_header"))) RenderAi();
    ImGui::Separator();
    RenderTests();
    if (v.lineCount > 0) {
        ImGui::Separator();
        ImGui::TextUnformatted(l10n::T("Recent scares"));
        for (int i = 0; i < v.lineCount; ++i) ImGui::TextDisabled("%s", v.lines[i]);
    }
}

}  // namespace ui::fear_pane
