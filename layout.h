#pragma once
#include <algorithm>

namespace layout {
struct Rect {
    int left = 0, top = 0, right = 0, bottom = 0;
};
struct Signal {
    Rect code, value, motion, content;
    bool stacked = false;
};
// All inputs and outputs are physical pixels. Cells remain square at any DPI.
inline Signal Compute(int width, int height, int top, int footer, int margin, int ratio,
                      bool moving, bool compact) {
    Signal r;
    r.content = {margin, top + margin, std::max(width - margin, margin + 8),
                 std::max(height - footer - margin, top + margin + 4)};
    const int w = r.content.right - r.content.left;
    const int h = r.content.bottom - r.content.top;
    const int gap = std::max(margin, 4);
    const int motionHeight = moving ? std::min(h / 3, std::max(h / 4, 60)) : 0;
    const int usable = std::max(h - motionHeight - (moving ? gap : 0), 4);
    r.stacked = ratio > 50;
    const int strip = r.stacked ? std::min(compact ? 38 : 54, usable / 3) : 0;
    const int maxCodeHeight = std::max(usable - strip - (r.stacked ? gap : 0), 4);
    int cw = std::min(w * std::clamp(ratio, 20, 90) / 100, maxCodeHeight * 2);
    cw = std::max(8, (cw / 8) * 8);
    const int ch = cw / 2;
    if (r.stacked) {
        const int x = r.content.left + (w - cw) / 2;
        const int y = r.content.top + strip + gap + (maxCodeHeight - ch) / 2;
        r.code = {x, y, x + cw, y + ch};
        r.value = {r.content.left, r.content.top, r.content.right, r.content.top + strip};
    } else {
        const int x = r.content.right - cw;
        const int y = r.content.top + (usable - ch) / 2;
        r.code = {x, y, x + cw, y + ch};
        r.value = {r.content.left, r.content.top, std::max(x - gap, r.content.left + 8),
                   r.content.top + usable};
    }
    if (moving)
        r.motion = {r.content.left, r.content.bottom - motionHeight, r.content.right,
                    r.content.bottom};
    return r;
}
} // namespace layout
