#include "emuscale/windows/DisplayInventory.h"
#include <shellscalingapi.h>
#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>

namespace emuscale {
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), n, nullptr, nullptr);
    return out;
}
std::string json_string(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
        else out << c;
    }
    out << '"';
    return out.str();
}
std::wstring window_display(HWND window) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!IsWindow(window) || !GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info)) return L"unknown";
    return info.szDevice;
}
std::vector<DisplayInfo> discover_displays(bool include_modes) {
    std::vector<DisplayInfo> result;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR h, HDC, LPRECT, LPARAM data) -> BOOL {
        auto& items = *reinterpret_cast<std::vector<DisplayInfo>*>(data);
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(h, &info)) return TRUE;
        DisplayInfo d;
        d.handle = h; d.device = info.szDevice; d.bounds = info.rcMonitor; d.work = info.rcWork;
        d.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        DEVMODEW mode{}; mode.dmSize = sizeof(mode);
        if (EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode)) {
            d.width = mode.dmPelsWidth; d.height = mode.dmPelsHeight;
            d.refresh_hz = mode.dmDisplayFrequency > 1 ? mode.dmDisplayFrequency : 0;
        }
        // A hidden, non-activating per-monitor-aware helper lets GetDpiForWindow
        // report each display's scaling without touching the captured window.
        HWND helper = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"", WS_POPUP,
            info.rcMonitor.left, info.rcMonitor.top, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (helper) { d.dpi_x = d.dpi_y = GetDpiForWindow(helper); DestroyWindow(helper); }
        DISPLAY_DEVICEW device{}; device.cb = sizeof(device);
        if (EnumDisplayDevicesW(info.szDevice, 0, &device, EDD_GET_DEVICE_INTERFACE_NAME)) {
            d.name = device.DeviceString; d.identifier = device.DeviceID;
        }
        items.push_back(std::move(d));
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    UINT path_count = 0, mode_count = 0;
    // DisplayConfig supplies panel names, persistent device paths and rational refresh rates.
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS) break;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
        const auto status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count, modes.data(), nullptr);
        if (status == ERROR_INSUFFICIENT_BUFFER) continue;
        if (status != ERROR_SUCCESS) break;
        for (UINT i = 0; i < path_count; ++i) {
            const auto& p = paths[i];
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source), p.sourceInfo.adapterId, p.sourceInfo.id};
            if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) continue;
            DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
            target.header = {DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(target), p.targetInfo.adapterId, p.targetInfo.id};
            for (auto& d : result) if (d.device == source.viewGdiDeviceName) {
                if (DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS) {
                    if (*target.monitorFriendlyDeviceName) d.name = target.monitorFriendlyDeviceName;
                    if (*target.monitorDevicePath) d.identifier = target.monitorDevicePath;
                }
                if (p.targetInfo.refreshRate.Denominator)
                    d.refresh_hz = double(p.targetInfo.refreshRate.Numerator) / p.targetInfo.refreshRate.Denominator;
            }
        }
        break;
    }
    if (include_modes) for (auto& d : result) {
        std::set<std::wstring> unique;
        DEVMODEW mode{}; mode.dmSize = sizeof(mode);
        for (DWORD i = 0; i < 4096 && EnumDisplaySettingsW(d.device.c_str(), i, &mode); ++i)
            unique.insert(std::to_wstring(mode.dmPelsWidth) + L"x" + std::to_wstring(mode.dmPelsHeight) +
                L"@" + std::to_wstring(mode.dmDisplayFrequency) + L"Hz/" + std::to_wstring(mode.dmBitsPerPel) + L"bpp");
        d.modes.assign(unique.begin(), unique.end());
    }
    return result;
}
std::string displays_json(const std::vector<DisplayInfo>& displays) {
    std::ostringstream out;
    out << '[';
    bool first = true;
    for (const auto& d : displays) {
        if (!first) out << ',';
        first = false;
        out << "{\"device\":" << json_string(utf8(d.device)) << ",\"name\":" << json_string(utf8(d.name))
            << ",\"identifier\":" << json_string(utf8(d.identifier)) << ",\"primary\":" << (d.primary ? "true" : "false")
            << ",\"width\":" << d.width << ",\"height\":" << d.height << ",\"refresh_hz\":" << d.refresh_hz
            << ",\"dpi_x\":" << d.dpi_x << ",\"dpi_y\":" << d.dpi_y
            << ",\"scale_percent\":" << d.dpi_x * 100.0 / 96.0
            << ",\"left\":" << d.bounds.left << ",\"top\":" << d.bounds.top << ",\"modes\":[";
        for (std::size_t i = 0; i < d.modes.size(); ++i) {
            if (i) out << ',';
            out << json_string(utf8(d.modes[i]));
        }
        out << "]}";
    }
    out << ']';
    return out.str();
}
}
