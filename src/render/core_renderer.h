#pragma once
#include "core/types.h"
#include <vector>
#include <algorithm>
#include <cstdint>

namespace bmv {

// ── Abstract interfaces ──

struct Viewport {
    int width  = 0;
    int height = 0;
    virtual int  y_at(tick_t tick) const = 0;
    virtual ~Viewport() = default;
};

struct PixelBuf {
    virtual void fill_rect(int x, int y, int w, int h, uint32_t color) = 0;
    virtual ~PixelBuf() = default;
};

// ── Color configs ──

struct NoteColors {
    uint32_t scratch = 0;
    uint32_t white   = 0;
    uint32_t blue    = 0;
    uint32_t ln_tail = 0;
};

struct GridColors {
    uint32_t bg_full   = 0;
    uint32_t bg_a      = 0;
    uint32_t bg_b      = 0;
    uint32_t measure   = 0;
    uint32_t fourth    = 0;
    uint32_t eighth    = 0;
    uint32_t sixteenth = 0;
    uint32_t lane      = 0;
    uint32_t judgment  = 0;
};

// ── Shared drawing routines ──

class CoreRenderer {
public:
    // Measure backgrounds + grid + lane separators + judgment line
    static void draw_background(PixelBuf& buf, const Viewport& vp,
        tick_t min_tick, tick_t max_tick,
        float chart_x0, int chart_w, int num_lanes, float lane_w,
        const GridColors& gc);

    // Note heads + LN bodies + LN tail markers
    static void draw_notes(PixelBuf& buf, const Viewport& vp,
        const std::vector<NoteEvent>& notes, const int* lane_map,
        tick_t min_tick, tick_t max_tick,
        float chart_x0, float lane_w, float note_w, float note_min_h,
        double ppt, const NoteColors& nc, int num_lanes);

    // Replay hit hollow boxes
    static void draw_replay_hits(PixelBuf& buf, const Viewport& vp,
        const std::vector<ReplayHit>& hits,
        tick_t min_tick, tick_t max_tick,
        float chart_x0, float lane_w, float box_w,
        double ppt, int num_lanes, uint32_t box_color);
};

// ── Inline implementations ──

inline uint32_t core_note_color(uint8_t lane, const NoteColors& nc) {
    switch (lane) {
        case 0:                return nc.scratch;
        case 1: case 3: case 5: case 7: return nc.white;
        default:               return nc.blue;
    }
}

inline void CoreRenderer::draw_background(PixelBuf& buf, const Viewport& vp,
    tick_t min_tick, tick_t max_tick,
    float chart_x0, int chart_w, int num_lanes, float lane_w,
    const GridColors& gc)
{
    int x0 = static_cast<int>(chart_x0);
    int x1 = x0 + chart_w;
    int vw = x1 - x0;

    // Measure backgrounds
    const tick_t kMeas = TICKS_PER_MEASURE;
    tick_t m_begin = min_tick / kMeas;
    tick_t m_end   = (max_tick / kMeas) + 1;
    for (tick_t m = m_begin; m <= m_end; ++m) {
        uint32_t bg = (m % 2 == 0) ? gc.bg_a : gc.bg_b;
        int y0 = vp.y_at(m * kMeas + kMeas);  // top of measure (smaller Y)
        int y1 = vp.y_at(m * kMeas);          // bottom of measure (larger Y)
        if (y1 <= y0) continue;
        buf.fill_rect(x0, y0, vw, y1 - y0, bg);
    }

    // Grid lines (single pass: measure separators + 4th + 8th + 16th)
    tick_t t_beg = (min_tick / kMeas) * kMeas;
    tick_t t_end = (max_tick / TPB) * TPB + TPB;
    for (tick_t t = t_beg; t <= t_end; t += TPB) {
        int y = vp.y_at(t);
        bool is_meas = (t % kMeas == 0);
        uint32_t col;
        int thk;
        if (is_meas)          { col = gc.measure;   thk = 2; }
        else if (t % (TPB*4) == 0) { col = gc.fourth;    thk = 1; }
        else if (t % (TPB*2) == 0) { col = gc.eighth;    thk = 1; }
        else                  { col = gc.sixteenth; thk = 1; }
        buf.fill_rect(x0, y, vw, thk, col);
    }

    // Lane separators (vertical)
    for (int lane = 0; lane <= num_lanes; ++lane) {
        int lx = static_cast<int>(chart_x0 + lane * lane_w);
        int thk = (lane == 0 || lane == num_lanes) ? 2 : 1;
        buf.fill_rect(lx, 0, thk, vp.height, gc.lane);
    }

    // Judgment line
    int jy = vp.height - 50;  // kPadBottom
    buf.fill_rect(x0, jy, vw, 2, gc.judgment);
}

inline void CoreRenderer::draw_notes(PixelBuf& buf, const Viewport& vp,
    const std::vector<NoteEvent>& notes, const int* lane_map,
    tick_t min_tick, tick_t max_tick,
    float chart_x0, float lane_w, float note_w, float note_min_h,
    double ppt, const NoteColors& nc, int num_lanes)
{
    for (auto& n : notes) {
        if (n.end_tick < min_tick && n.tick < min_tick) continue;
        if (n.tick > max_tick) continue;

        int render_lane = lane_map ? lane_map[n.lane] : static_cast<int>(n.lane);
        if (render_lane >= num_lanes) continue;

        uint32_t color = core_note_color(n.lane, nc);

        float cx = chart_x0 + render_lane * lane_w + (lane_w - note_w) * 0.5f;
        int cy = vp.y_at(n.tick);
        int nh = std::max(static_cast<int>(note_min_h),
                          std::max(1, static_cast<int>(ppt * 6.0)));
        int ny = cy - nh / 2;
        buf.fill_rect(static_cast<int>(cx), ny,
                      static_cast<int>(note_w), nh, color);

        // LN body
        if (n.end_tick > n.tick) {
            int body_x = static_cast<int>(cx + note_w * 0.3f);
            int body_w = std::max(1, static_cast<int>(note_w * 0.4f));
            int tail_y = vp.y_at(n.end_tick);
            int body_y0 = cy + nh / 2;  // bottom of head rect
            int body_y1 = tail_y;       // Y at tail position

            // Clamp body to viewport height
            if (body_y1 > vp.height) body_y1 = vp.height;
            if (body_y0 < 0) body_y0 = 0;

            if (body_y1 < body_y0) {
                buf.fill_rect(body_x, body_y1, body_w, body_y0 - body_y1, color);
            }

            // LN tail marker
            int tail_mark_y = tail_y - 2;
            if (tail_mark_y < 0) tail_mark_y = 0;
            if (tail_mark_y + 4 > vp.height) tail_mark_y = vp.height - 4;
            buf.fill_rect(static_cast<int>(cx), tail_mark_y,
                          static_cast<int>(note_w), 4, nc.ln_tail);
        }
    }
}

inline void CoreRenderer::draw_replay_hits(PixelBuf& buf, const Viewport& vp,
    const std::vector<ReplayHit>& hits,
    tick_t min_tick, tick_t max_tick,
    float chart_x0, float lane_w, float box_w,
    double ppt, int num_lanes, uint32_t box_color)
{
    for (auto& hit : hits) {
        if (hit.tick_end < min_tick && hit.tick_start < min_tick) continue;
        if (hit.tick_start > max_tick) continue;
        if (hit.lane >= static_cast<uint8_t>(num_lanes)) continue;

        tick_t ts = hit.tick_start;
        tick_t te = (hit.tick_end > hit.tick_start) ? hit.tick_end : (hit.tick_start + 1);

        int y0 = vp.y_at(ts);
        int y1 = vp.y_at(te);

        // Clamp to viewport
        if (y1 > vp.height) y1 = vp.height;
        if (y0 < 0) y0 = 0;

        int bh = y0 - y1;
        int min_h = std::max(2, static_cast<int>(ppt * 4.0));
        if (bh < min_h) bh = min_h;

        int bx = static_cast<int>(chart_x0 + hit.lane * lane_w
                  + (lane_w - box_w) * 0.5f);
        int by = y1;

        // Hollow rectangle: 4 lines
        int thick = 1;
        buf.fill_rect(bx, by, static_cast<int>(box_w), thick, box_color);           // top
        buf.fill_rect(bx, by + bh - thick, static_cast<int>(box_w), thick, box_color); // bottom
        buf.fill_rect(bx, by, thick, bh, box_color);                                 // left
        buf.fill_rect(bx + static_cast<int>(box_w) - thick, by, thick, bh, box_color); // right
    }
}

} // namespace bmv
