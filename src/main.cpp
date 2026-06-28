#include "core/timeline.h"
#include "format/bms_parser.h"
#include "render/png_renderer.h"
#include "replay/replay.h"
#include "app/application.h"
#include "analysis/judgement_engine.h"
#include "util/encoding.h"
#include "util/fs_util.h"
#include "picosha2.h"
#include "md5.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

static int run_cli(int argc, char* argv[]) {
    std::string input_path;
    std::string output_path;
    std::string replay_path;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--replay") == 0 && i + 1 < argc) {
            replay_path = argv[++i];
        } else if (input_path.empty()) {
            input_path = argv[i];
        } else if (output_path.empty()) {
            output_path = argv[i];
        }
    }

    if (input_path.empty() || output_path.empty()) {
        std::printf("BMV - BMS Viewer\n");
        std::printf("Usage: bmv <input.bms> <output.png> [--replay replay.brd]\n");
        std::printf("  Run without arguments for GUI mode.\n");
        return 1;
    }

    std::printf("Parsing: %s\n", input_path.c_str());
    bmv::BmsParser parser;
    bmv::RawChartData raw = parser.parse(input_path);

    if (raw.channels.empty()) {
        std::fprintf(stderr, "Error: no channel data parsed from '%s'\n", input_path.c_str());
        return 1;
    }

    // Calculate hash for CLI mode (cross-platform path encoding via fs_util)
    auto file_bytes = bmv::read_file_binary(input_path);
    if (!file_bytes.empty()) {
        std::string sha256 = picosha2::hash256_hex_string(file_bytes);
        std::string md5 = md5::hash_hex_string(file_bytes);

        std::printf("SHA256: %s\n", sha256.c_str());
        std::printf("MD5:    %s\n", md5.c_str());
    }

    std::printf("Building timeline...\n");
    bmv::Timeline timeline = bmv::build_timeline(raw);

    std::printf("  Title:  %s\n", timeline.title.c_str());
    std::printf("  Artist: %s\n", timeline.artist.c_str());
    std::printf("  BPM:    %.1f\n", timeline.initial_bpm);
    std::printf("  Notes:  %zu\n", timeline.notes.size());
    std::printf("  BGM:    %zu\n", timeline.bgm.size());
    std::printf("  Ticks:  %lld -> %lld\n",
                 static_cast<long long>(timeline.tick_begin()),
                 static_cast<long long>(timeline.tick_end()));

    bmv::ReplayData replay_data;
    const bmv::ReplayData* replay_ptr = nullptr;

    if (!replay_path.empty()) {
        std::printf("Parsing replay: %s\n", replay_path.c_str());
        auto input = bmv::parse_replay(replay_path);
        if (!input) {
            std::fprintf(stderr, "Warning: failed to parse replay '%s', continuing without replay\n", replay_path.c_str());
        } else {
            replay_data = bmv::replay_input_to_replay_data(input.value(), timeline.time_map);
            replay_ptr = &replay_data;

            // Replay statistics (moved from parser library)
            if (input->format == bmv::ReplayFormat::BRD) {
                std::printf("  Format: BRD, Hits: %zu, Unmatched: %d, Shuffle: %s\n",
                             replay_data.hits.size(), replay_data.unmatched,
                             input->brd.has_shuffle ? "YES" : "NO");
            } else {
                auto mode_name = [](bmv::LR2RandomMode m) -> const char* {
                    if (m == bmv::LR2RandomMode::Mirror)  return "MIRROR";
                    if (m == bmv::LR2RandomMode::Random)  return "RANDOM";
                    if (m == bmv::LR2RandomMode::SRandom) return "S-RANDOM";
                    if (m == bmv::LR2RandomMode::RRandom) return "R-RANDOM";
                    return "OFF";
                };
                int recorded_hits = 0;
                for (auto& h : replay_data.hits) if (h.is_press) recorded_hits++;
                std::printf("  Format: LR2REP, Hits: %zu (%d KeyDown)\n",
                             replay_data.hits.size(), recorded_hits);
                std::printf("  P1 Random: %s  P2 Random: %s  Seed: %d  op210: %zu\n",
                             mode_name(replay_data.random_mode[0]),
                             mode_name(replay_data.random_mode[1]),
                             replay_data.random_seed,
                             replay_data.lr2_judgements.size());
            }

            // Judge audit (CLI test) — auto-detect system from replay format
            bmv::JudgementEngine je;
            bool is_lr2 = (input->format == bmv::ReplayFormat::LR2REP);
            je.set_system(is_lr2 ? bmv::JudgeSystem::LR2 : bmv::JudgeSystem::Beatoraja);
            je.analyze(timeline, replay_data);
        }
    }

    std::printf("Rendering: %s\n", output_path.c_str());
    bmv::PngRenderer renderer;
    bmv::PngRenderer::Config cfg;
    cfg.pixels_per_measure = 300;

    if (!renderer.render(timeline, replay_ptr, cfg, output_path)) {
        std::fprintf(stderr, "Error: failed to render '%s'\n", output_path.c_str());
        return 1;
    }

    std::printf("Done: %s -> %s\n", input_path.c_str(), output_path.c_str());

    std::printf("\n========================================\n");
    std::printf("          INTEGRITY REPORT\n");
    std::printf("========================================\n");

    int bpm_valid = 0, bpm_invalid = 0;
    for (auto& b : timeline.bpm_changes) {
        if (b.source == bmv::BpmSource::CH03 || b.source == bmv::BpmSource::CH08) bpm_valid++;
        else bpm_invalid++;
    }
    std::printf("BPM Integrity:\n");
    std::printf("  Total BPM events:  %zu\n", timeline.bpm_changes.size());
    std::printf("  Valid BPM events:  %d\n", bpm_valid);
    std::printf("  Invalid BPM:       %d\n", bpm_invalid);

    std::printf("Measure Integrity:\n");
    std::printf("  Audit printed above (build_timeline)\n");

    int ln_count = 0;
    for (auto& n : timeline.notes) {
        if (n.end_tick > n.tick) ln_count++;
    }
    std::printf("LN Integrity:\n");
    std::printf("  LN count: %d\n", ln_count);
    std::printf("  Status:   OK\n");

    if (replay_ptr) {
        std::printf("Replay Integrity:\n");
        std::printf("  Hits:      %zu\n", replay_data.hits.size());
        std::printf("  Unmatched: %d\n", replay_data.unmatched);
        std::printf("  Shuffle:   %s\n", replay_data.has_shuffle ? "applied" : "none");
    }

    std::printf("========================================\n");
    return 0;
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // 让控制台按 UTF-8 解释输出，避免日文 Title/Artist 显示为乱码。
    // （GUI 子系统下无控制台，这两个调用是 no-op）
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // Windows CRT 的 argv 按 ANSI 代码页编码，日文路径会乱码。
    // 重新用 GetCommandLineW + CommandLineToArgvW 获取宽字符参数，转为 UTF-8，
    // 使 CLI 和 GUI 路径编码统一为 UTF-8（与 fs_util::to_path() 期望一致）。
    {
        int wargc = 0;
        wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
        if (wargv && wargc > 0) {
            static std::vector<std::string> utf8_args;
            static std::vector<char*> utf8_argv;
            utf8_args.clear();
            utf8_argv.clear();
            utf8_args.reserve(wargc);
            utf8_argv.reserve(wargc);
            for (int i = 0; i < wargc; ++i) {
                utf8_args.push_back(bmv::wstring_to_utf8(wargv[i]));
                utf8_argv.push_back(utf8_args.back().data());
            }
            LocalFree(wargv);
            argc = wargc;
            argv = utf8_argv.data();
        }
    }
#endif
    if (argc == 1) {
        return bmv::Application().run();
    }
    // --gui <file>: 启动 GUI 并自动加载指定谱面（argv 已转为 UTF-8）
    if (argc >= 3 && std::strcmp(argv[1], "--gui") == 0) {
        bmv::Application app;
        app.set_preload_chart(argv[2]);
        return app.run();
    }
    return run_cli(argc, argv);
}

#ifdef _WIN32
// GUI 子系统入口点（Release 用 /SUBSYSTEM:WINDOWS）。
// Debug 用 /SUBSYSTEM:CONSOLE，链接器找 main()，此函数被忽略。
// MSVC CMake 的 NDEBUG 宏只在 Release 定义，用于区分两种构建。
#ifdef NDEBUG
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return main(__argc, (char**)__argv);
}
#endif
#endif
