#pragma once
#include <windows.h>
#include <vector>

namespace emuscale {
struct Extent { unsigned width = 0, height = 0; };
struct Viewport { double x = 0, y = 0, width = 0, height = 0; };
Viewport aspect_viewport(Extent source, Extent destination);
POINT desktop_to_monitor(POINT point, const RECT& monitor);
POINT monitor_to_desktop(POINT point, const RECT& monitor);
int monitor_for_rect(const RECT& window, const std::vector<RECT>& monitors);
RECT visible_preview_rect(const RECT& window, const std::vector<RECT>& work_areas);
}
