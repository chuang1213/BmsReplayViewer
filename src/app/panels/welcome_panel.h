#pragma once

namespace bmv {

enum class WelcomeAction { None, OpenChart, OpenReplay };

WelcomeAction render_welcome_panel();

} // namespace bmv
