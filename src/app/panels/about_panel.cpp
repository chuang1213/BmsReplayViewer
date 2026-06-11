#include "about_panel.h"
#include "imgui.h"

namespace bmv {

void render_about_panel() {
    ImGui::Text("BMV -- BMS Visualizer");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Version: 0.3.3");
    ImGui::Text("Build:   " __DATE__);
    ImGui::Text("By Chuang1227(chuang1227@foxmail.com)");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Third-party Libraries:");
    ImGui::Indent();
    ImGui::BulletText("Dear ImGui (ocornut)");
    ImGui::BulletText("GLFW (glfw.org)");
    ImGui::BulletText("nlohmann/json");
    ImGui::BulletText("zlib (gzip)");
    ImGui::BulletText("Dr.Libs (base64)");
    ImGui::Unindent();
    ImGui::Spacing();

    ImGui::Text("A replay analysis and visualization tool\nfor BMS simulation.");
}

} // namespace bmv
