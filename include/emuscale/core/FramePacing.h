#pragma once
#include <cstdint>

namespace emuscale {
enum class FpsLimit : std::uint8_t { automatic, fps30, fps60, fps90, fps120,
                                    fps144, fps165, fps240, unlocked };
enum class VSyncMode : std::uint8_t { automatic, on, off };

[[nodiscard]] int fps_cap(FpsLimit limit) noexcept;
[[nodiscard]] const wchar_t* fps_limit_name(FpsLimit limit) noexcept;
[[nodiscard]] const wchar_t* vsync_name(VSyncMode mode) noexcept;
struct PresentPolicy {
    unsigned sync_interval = 1;
    bool allow_tearing = false;
};
[[nodiscard]] PresentPolicy present_policy(VSyncMode mode,
                                           bool tearing_supported) noexcept;

// The cap is a maximum. Every call corresponds to a newly delivered capture
// frame; this policy never asks the renderer to repeat an old frame.
class FramePacer {
public:
    void set_limit(FpsLimit limit) noexcept;
    [[nodiscard]] FpsLimit limit() const noexcept { return limit_; }
    [[nodiscard]] bool allow(std::int64_t now_qpc, std::int64_t frequency) noexcept;
private:
    FpsLimit limit_ = FpsLimit::automatic;
    std::int64_t last_present_qpc_ = 0;
};
} // namespace emuscale
