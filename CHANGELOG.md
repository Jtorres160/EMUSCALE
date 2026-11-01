# Changelog

## Unreleased

## 0.1.2 — 2026-11-01

- Added automatic visible Eden process/window targeting, explicit HWND selection for ambiguous targets, and user-declared TotK/scenario/optimizer/scene labels. Tests B/C require explicit readiness; emulator and optimizer settings are never changed.
- Added multi-display names/identifiers, active count, bounds, modes, resolution, rational refresh and DPI inventory, plus sampled target/preview monitor assignments. Diagnostic startup shows the preview on another display without activation and leaves settings hidden.
- Added independent one-second diagnostic sampling and progressive CSV/JSONL logs with bounded event/history buffers, exclusive storage accounting, 32 MiB session/256 MiB directory quotas, clean finalization and partial-session analysis.
- Added process CPU/working set and optional Windows PDH GPU engine measurements, cumulative >50 ms presentation spikes, WGC-to-Present-return timing, sampled logging cost and a native `emuscale_analyze` executable with automatic post-session reports.
- Added optional predefined gameplay marker buttons/commands and a graceful stop command, without keyboard hooks or stored gameplay images.
- Preserved v0.1.1 manual capture, settings, presentation caps and timed benchmark CSVs. Corrected missing-timestamp accounting, excluded occluded Present returns, and coalesced capture notifications around the one-frame mailbox.
- Added diagnostic integration/validation tests and enabled assertions for tests in Release builds. See the README validation record for measured live results and limitations.
- Validated MSVC x64 Debug/Release and 10/10 CTest cases in both configurations. A 61.01 s Release Eden/TotK baseline averaged 30.66 timestamp-distinct capture and presented FPS, with zero mailbox overwrites/interruptions and 15 presentation intervals over 50 ms. The startup audit observed unchanged Eden bounds and foreground; tests B/C were not run.

## 0.1.1

- Added v0.1.1 FPS controls (Auto, 30–240, Unlocked), VSync modes with conditional DXGI tearing, a Win32 settings window, capture/presentation pacing metrics, and timed CSV benchmarks. Verified the controls and live Eden capture on an RTX 4080 SUPER; no upscaling or frame generation is implemented.
- Validated v0.1 live Eden capture, preview resizing, capture-size changes, and clean exit on target closure on an RTX 4080 SUPER; build and CTest passed.
- Added v0.1 Win32 window discovery, Windows Graphics Capture session, D3D11 live preview, and capture/presentation statistics.
- Broadened the intended GPU target to GTX 900 through RTX 50, with feature-specific gates.
- Added DXGI/D3D11 GPU inventory, separate hardware/SDK/integration capability states, backend policy, compatibility mode, and future processing interfaces.
- Documented the capture-first roadmap and the boundary between custom interpolation and native NVIDIA DLSS features.
