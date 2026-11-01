#include "emuscale/windows/DisplayPolicy.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace emuscale {
Viewport aspect_viewport(Extent source, Extent destination) {
    if (!source.width || !source.height || !destination.width || !destination.height) return {};
    const double scale = std::min(double(destination.width) / source.width, double(destination.height) / source.height);
    const double w = source.width * scale, h = source.height * scale;
    return {(destination.width - w) / 2, (destination.height - h) / 2, w, h};
}
POINT desktop_to_monitor(POINT p, const RECT& monitor) { return {p.x - monitor.left, p.y - monitor.top}; }
POINT monitor_to_desktop(POINT p, const RECT& monitor) { return {p.x + monitor.left, p.y + monitor.top}; }
int monitor_for_rect(const RECT& r, const std::vector<RECT>& monitors) {
    int best = -1; double best_area = -1, best_distance = std::numeric_limits<double>::max();
    const double x = (double(r.left) + r.right) / 2, y = (double(r.top) + r.bottom) / 2;
    for (std::size_t i = 0; i < monitors.size(); ++i) {
        const auto& m = monitors[i];
        const double area = double(std::max<LONG>(0, std::min(r.right, m.right) - std::max(r.left, m.left))) *
            std::max<LONG>(0, std::min(r.bottom, m.bottom) - std::max(r.top, m.top));
        const double dx = x - std::clamp(x, double(m.left), double(m.right));
        const double dy = y - std::clamp(y, double(m.top), double(m.bottom));
        const double distance = dx * dx + dy * dy;
        if (area > best_area || (area == best_area && distance < best_distance)) {
            best = static_cast<int>(i); best_area = area; best_distance = distance;
        }
    }
    return best;
}
RECT visible_preview_rect(const RECT& r, const std::vector<RECT>& work) {
    for (const auto& m : work) {
        RECT intersection{};
        if (IntersectRect(&intersection, &r, &m) && intersection.right - intersection.left >= 64 &&
            intersection.bottom - intersection.top >= 32) return r;
    }
    const int index = monitor_for_rect(r, work);
    if (index < 0) return r;
    const auto& m = work[index];
    const LONG width = std::clamp<LONG>(r.right - r.left, 1, std::max<LONG>(1, m.right - m.left));
    const LONG height = std::clamp<LONG>(r.bottom - r.top, 1, std::max<LONG>(1, m.bottom - m.top));
    const LONG x = std::clamp(r.left, m.left, m.right - width), y = std::clamp(r.top, m.top, m.bottom - height);
    return {x, y, x + width, y + height};
}
}
