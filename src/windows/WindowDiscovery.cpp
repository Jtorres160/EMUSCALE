#include "emuscale/windows/WindowDiscovery.h"

#include <algorithm>
#include <cwctype>
#include <iterator>
#include <string_view>

namespace emuscale {
namespace {
std::wstring process_name(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return L"<inaccessible>";
    wchar_t path[32768]{};
    DWORD length = static_cast<DWORD>(std::size(path));
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &length) != FALSE;
    CloseHandle(process);
    if (!ok) return L"<inaccessible>";
    std::wstring_view full(path, length);
    const auto slash = full.find_last_of(L"\\/");
    return std::wstring(full.substr(slash == std::wstring_view::npos ? 0 : slash + 1));
}

bool emulator_name(std::wstring name) {
    std::transform(name.begin(), name.end(), name.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    for (const auto* candidate : {L"eden", L"yuzu", L"ryujinx", L"dolphin", L"cemu", L"rpcs3"})
        if (name.find(candidate) != std::wstring::npos) return true;
    return false;
}

BOOL CALLBACK enumerate(HWND handle, LPARAM data) {
    auto& windows = *reinterpret_cast<std::vector<WindowInfo>*>(data);
    if (!IsWindowVisible(handle) || GetWindow(handle, GW_OWNER) != nullptr) return TRUE;
    const int length = GetWindowTextLengthW(handle);
    if (length <= 0) return TRUE;
    std::wstring title(static_cast<std::size_t>(length) + 1, L'\0');
    const int actual = GetWindowTextW(handle, title.data(), length + 1);
    if (actual <= 0) return TRUE;
    title.resize(actual);
    DWORD pid = 0;
    GetWindowThreadProcessId(handle, &pid);
    if (!pid || pid == GetCurrentProcessId()) return TRUE;
    auto name = process_name(pid);
    windows.push_back({handle, pid, std::move(title), name,
                       IsIconic(handle) != FALSE, emulator_name(name)});
    return TRUE;
}
} // namespace

std::vector<WindowInfo> discover_windows() {
    std::vector<WindowInfo> result;
    EnumWindows(enumerate, reinterpret_cast<LPARAM>(&result));
    std::stable_partition(result.begin(), result.end(),
                          [](const WindowInfo& window) { return window.emulator; });
    return result;
}

bool capturable_window(HWND handle) noexcept {
    return handle && IsWindow(handle) && IsWindowVisible(handle) && !IsIconic(handle);
}
} // namespace emuscale
