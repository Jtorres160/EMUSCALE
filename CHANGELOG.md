# Changelog

## Unreleased

- Added v0.1.1 FPS controls (Auto, 30–240, Unlocked), VSync modes with conditional DXGI tearing, a Win32 settings window, capture/presentation pacing metrics, and timed CSV benchmarks. Verified the controls and live Eden capture on an RTX 4080 SUPER; no upscaling or frame generation is implemented.
- Validated v0.1 live Eden capture, preview resizing, capture-size changes, and clean exit on target closure on an RTX 4080 SUPER; build and CTest passed.
- Added v0.1 Win32 window discovery, Windows Graphics Capture session, D3D11 live preview, and capture/presentation statistics.
- Broadened the intended GPU target to GTX 900 through RTX 50, with feature-specific gates.
- Added DXGI/D3D11 GPU inventory, separate hardware/SDK/integration capability states, backend policy, compatibility mode, and future processing interfaces.
- Documented the capture-first roadmap and the boundary between custom interpolation and native NVIDIA DLSS features.
