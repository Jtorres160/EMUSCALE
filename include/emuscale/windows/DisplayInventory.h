#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace emuscale {
struct DisplayInfo {
    HMONITOR handle = nullptr;
    std::wstring device, name, identifier;
    RECT bounds{}, work{};
    unsigned width = 0, height = 0, dpi_x = 0, dpi_y = 0;
    double refresh_hz = 0;
    bool primary = false;
    std::vector<std::wstring> modes;
};
std::vector<DisplayInfo> discover_displays(bool include_modes = true);
std::wstring window_display(HWND window);
std::string utf8(const std::wstring& value);
std::string json_string(const std::string& value);
std::string displays_json(const std::vector<DisplayInfo>& displays);
}
