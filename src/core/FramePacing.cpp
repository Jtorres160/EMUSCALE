#include "emuscale/core/FramePacing.h"

namespace emuscale {
int fps_cap(FpsLimit limit) noexcept {
    switch (limit) {
    case FpsLimit::fps30: return 30;
    case FpsLimit::fps60: return 60;
    case FpsLimit::fps90: return 90;
    case FpsLimit::fps120: return 120;
    case FpsLimit::fps144: return 144;
    case FpsLimit::fps165: return 165;
    case FpsLimit::fps240: return 240;
    default: return 0;
    }
}
const wchar_t* fps_limit_name(FpsLimit limit) noexcept {
    switch (limit) {
    case FpsLimit::automatic: return L"Auto";
    case FpsLimit::fps30: return L"30 FPS";
    case FpsLimit::fps60: return L"60 FPS";
    case FpsLimit::fps90: return L"90 FPS";
    case FpsLimit::fps120: return L"120 FPS";
    case FpsLimit::fps144: return L"144 FPS";
    case FpsLimit::fps165: return L"165 FPS";
    case FpsLimit::fps240: return L"240 FPS";
    case FpsLimit::unlocked: return L"Unlocked";
    }
    return L"Unknown";
}
const wchar_t* vsync_name(VSyncMode mode) noexcept {
    switch (mode) {
    case VSyncMode::automatic: return L"Auto (On)";
    case VSyncMode::on: return L"On";
    case VSyncMode::off: return L"Off";
    }
    return L"Unknown";
}
PresentPolicy present_policy(VSyncMode mode, bool tearing_supported) noexcept {
    if (mode == VSyncMode::off) return {0, tearing_supported};
    return {1, false}; // Auto preserves the v0.1 VSync-on behavior.
}
void FramePacer::set_limit(FpsLimit limit) noexcept {
    limit_ = limit;
    last_present_qpc_ = 0;
}
bool FramePacer::allow(std::int64_t now_qpc, std::int64_t frequency) noexcept {
    const int cap = fps_cap(limit_);
    if (cap == 0 || frequency <= 0) { last_present_qpc_ = now_qpc; return true; }
    const auto interval = frequency / cap;
    // A small tolerance avoids dropping a frame because of timer quantization.
    if (last_present_qpc_ != 0 && now_qpc - last_present_qpc_ < interval - interval / 20)
        return false;
    last_present_qpc_ = now_qpc;
    return true;
}
} // namespace emuscale
