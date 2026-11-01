#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace emuscale {
struct WindowInfo {
    HWND handle = nullptr;
    DWORD process_id = 0;
    std::wstring title;
    std::wstring process_name;
    bool minimized = false;
    bool emulator = false;
};

[[nodiscard]] std::vector<WindowInfo> discover_windows();
[[nodiscard]] bool capturable_window(HWND handle) noexcept;
} // namespace emuscale
