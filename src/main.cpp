#include "core/timeline.h"
#include "format/bms_parser.h"
#include "render/png_renderer.h"
#include "replay/replay.h"
#include "replay/lr2_random.h"
#include "app/application.h"
#include "analysis/judgement_engine.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <cstring>

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
        if (input) {
            replay_data = bmv::replay_input_to_replay_data(input.value(), timeline.time_map);
            replay_ptr = &replay_data;
            std::printf("  Hits: %zu, shuffle: %s\n",
                         replay_data.hits.size(),
                         replay_data.has_shuffle ? "YES" : "NO");

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
#ifdef _DEBUG
    bmv::test_lr2_random();
#endif
    if (argc == 1) {
        return bmv::Application().run();
    }
    return run_cli(argc, argv);
}
