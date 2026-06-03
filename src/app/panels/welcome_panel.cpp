#include "welcome_panel.h"
#include "imgui.h"

namespace bmv {

WelcomeAction render_welcome_panel() {
    WelcomeAction action = WelcomeAction::None;

    ImGui::Text("BMV -- BMS Visualizer");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::BulletText("Supported formats:");
    ImGui::Indent();
    ImGui::BulletText("Charts:  .bms  .bme  .bml  (.bmson not tested)");
    ImGui::BulletText("Replays: .brd  .lr2rep");
    ImGui::Unindent();
    ImGui::Spacing();

    ImGui::Text("Quick Start:");
    ImGui::Indent();
    ImGui::BulletText("1.Drag & drop a chart file onto the window first");
    ImGui::BulletText("2.Drag & drop a .brd/lr2rep file to load replay data");
    ImGui::BulletText("Or use File > Open Chart... & Open Replay...");
    ImGui::Unindent();
    ImGui::Spacing();

    ImGui::Text("Features:");
    ImGui::Indent();
    ImGui::BulletText("Chart Analysis");
    ImGui::BulletText("Replay Overlay");
    ImGui::BulletText("Video Export");
    ImGui::BulletText("please create an issue or email me for bug reports/feature requests");
    ImGui::Unindent();
    ImGui::Spacing();

    ImGui::Separator();
    if (ImGui::Button("Open Chart...", ImVec2(140, 0)))
        action = WelcomeAction::OpenChart;
    ImGui::SameLine();
    if (ImGui::Button("Open Replay...", ImVec2(140, 0)))
        action = WelcomeAction::OpenReplay;

    return action;
}

} // namespace bmv