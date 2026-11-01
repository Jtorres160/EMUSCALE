#include "emuscale/windows/DisplayPolicy.h"
#include <cassert>
#include <cmath>
int main() {
    using namespace emuscale;
    const std::vector<RECT> monitors{{0, 0, 2560, 1440}, {-1920, -200, 0, 880}};
    assert(monitor_for_rect({-1800, -100, -800, 600}, monitors) == 1);
    assert(monitor_for_rect({100, 100, 1000, 800}, monitors) == 0);
    assert(monitor_for_rect({-10000, 0, -9000, 800}, monitors) == 1);
    auto local = desktop_to_monitor({-1500, 0}, monitors[1]); assert(local.x == 420 && local.y == 200);
    auto global = monitor_to_desktop(local, monitors[1]); assert(global.x == -1500 && global.y == 0);
    auto fit = aspect_viewport({1920, 1080}, {1000, 1000});
    assert(std::abs(fit.width - 1000) < 1e-8 && std::abs(fit.height - 562.5) < 1e-8 && std::abs(fit.y - 218.75) < 1e-8);
    fit = aspect_viewport({1080, 1920}, {1920, 1080}); assert(std::abs(fit.width - 607.5) < .0001 && fit.x > 0);
    assert(aspect_viewport({0, 100}, {100, 100}).width == 0);
    // A removed negative-coordinate display clamps only the preview to remaining work.
    const auto restored = visible_preview_rect({-1800, 20, -800, 700}, {monitors[0]});
    assert(restored.left >= 0 && restored.right <= 2560 && restored.bottom <= 1440);
    const RECT visible{-1800, -100, -800, 600};
    const auto same = visible_preview_rect(visible, monitors); assert(EqualRect(&same, &visible));
    // Physical pixel extents from DPI/resolution changes still preserve aspect.
    fit = aspect_viewport({2560, 1440}, {1500, 900}); assert(std::abs(fit.width - 1500) < 1e-8 && std::abs(fit.height - 843.75) < 1e-8);
    const auto large = visible_preview_rect({0, 0, 4000, 2400}, {{-800, -600, 0, 0}});
    assert(large.right - large.left == 800 && large.bottom - large.top == 600);
    assert(monitor_for_rect({}, {}) == -1);
}
