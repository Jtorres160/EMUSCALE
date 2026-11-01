# EmuScale RTX

EmuScale RTX is a Windows emulator enhancement project for NVIDIA GTX 900, GTX 10 (including GTX 1080 Ti), GTX 16, RTX 20, RTX 30, RTX 40, and RTX 50 series GPUs. Each enhancement is conditional on its actual hardware, driver, OS, SDK, and application requirements. GPU branding alone does not enable a feature.

## Current state

This workspace was empty when the compatibility update began. It now contains a C++20 capability library, Windows DXGI/D3D11 GPU enumeration, a GPU information console tool, independent future backend interfaces, a conservative selection policy, and policy tests. The v0.1 executable includes an emulator window selector, Windows Graphics Capture session, and a D3D11 live preview. It has been built and tested against Eden on Windows; see the validation record below. NIS processing, DLSS, and frame generation remain disabled. See [the architecture](docs/ARCHITECTURE.md).

The detector enumerates physical DXGI adapters, records vendor/device IDs and D3D11 feature level, and returns separate `hardware`, `sdk`, and `integration` states for each backend. `unknown` is never treated as available. NIS is the intended default once its shader integration exists; today selection safely falls back to the unprocessed capture path. Conservative compatibility mode permits only verified NIS and otherwise uses that same path. Native and custom frame generation never substitute for each other silently.

## Intended compatibility

| GPU family | NIS spatial upscaling | Native DLSS Super Resolution | Custom interpolation | Native DLSS Frame Generation | Native DLSS Multi Frame Generation |
|---|---|---|---|---|---|
| GTX 900 / 10, including 1080 Ti | Planned, subject to D3D11/SDK validation | Ineligible | Compute or shader route requires research; NVIDIA Optical Flow SDK is not the Pascal/Maxwell path | Ineligible | Ineligible |
| GTX 16 | Planned, subject to validation | Ineligible | Optical Flow SDK eligibility varies by GPU; query it, with compute fallback research | Ineligible | Ineligible |
| RTX 20 / 30 | Planned, subject to validation | Future renderer integration, runtime SDK query required | Optical Flow SDK candidate, runtime query required | Ineligible | Ineligible |
| RTX 40 | Planned, subject to validation | Future renderer integration, runtime SDK query required | Optical Flow SDK candidate, runtime query required | Future renderer integration, runtime SDK query required | Ineligible |
| RTX 50 | Planned, subject to validation | Future renderer integration, runtime SDK query required | Optical Flow SDK candidate, runtime query required | Future renderer integration, runtime SDK query required | Future renderer integration, runtime SDK state query required |

This is a *target matrix*, not a list of currently working features. The detector intentionally reports uncertain SDK hardware support as `unknown` until the selected adapter is queried through the relevant SDK. The current `sdk` and `integration` columns are `unsupported` because those SDKs and processing paths are absent. NIS hardware support currently means only that a D3D11 level 11 device was created; shader format and dispatch validation remain for integration.

NIS uses the current color frame and is appropriate for external capture. Native DLSS Super Resolution needs renderer data such as motion vectors and temporal state. A captured image alone cannot supply those inputs. Use currently supported DLSS SDK models when renderer integration is built, selecting a quality/performance mode based on measured cost and SDK support. Do not force old DLSS 1 or 2 implementations. Keep custom interpolation separate from NVIDIA DLSS Frame Generation.

## Build and run v0.1

Install Visual Studio 2022 with **Desktop development with C++**, the MSVC x64 toolset, a Windows 10/11 SDK version 19041 or newer (including C++/WinRT headers), and CMake tools. Windows 10 version 1903 or newer is required for window capture; Windows 11 is supported. In a Developer PowerShell for VS 2022:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Launch Eden, leave its game window visible and restored, then run `build\Debug\emuscale_rtx.exe`. Select the numbered Eden window from the console list. A separate preview window should display live captured frames. Its title reports the active GPU, captured and presented FPS, overwritten frames, and capture/output dimensions. Close either window to stop; restarting the program is required to choose a different target. Other visible top-level application windows can also be selected. Run `build\Debug\emuscale_gpu_info.exe` to inspect detected adapters and independent capability states; add `--compatibility=conservative` for compatibility mode (`automatic` is the default).

The preview is SDR BGRA8. Protected content, minimized targets, inaccessible windows, and capture-disabled systems may produce no frames or an error. The program makes no claim about end-to-end input latency or GPU processing cost. An overwritten frame count measures frames replaced in the application's one-frame mailbox; it is not a count of every frame lost upstream. Qt 6 and NVIDIA SDKs are not required for v0.1. The build requires Windows; non-Windows configuration fails intentionally.

### Validation record — 2026-11-01

On Windows with an NVIDIA GeForce RTX 4080 SUPER, the x64 Debug build passed and CTest passed (1/1). Eden v0.2.1 was detected with The Legend of Zelda: Tears of the Kingdom running. The preview showed changing gameplay frames at roughly 27–32 captured FPS and 27–32 presented FPS, with zero application mailbox overwrites in the observed samples. Resizing the preview from 1264×721 to 757×721 kept the image visible and aspect-correct. Changing Eden's capture dimensions from 2560×1440 to 1922×1157 did not interrupt capture. After Eden was closed through its normal confirmation dialog, the preview disappeared and `emuscale_rtx.exe` exited with code 0. This single session does not establish long-term leak or performance behavior on other GPUs and emulators.

## Next development milestones

1. **v0.1 capture:** Eden selection, real preview frames, aspect-preserving resize, changing capture dimensions, source closure, and clean process exit were observed on one Windows system. Device removal and long-duration resource use still need targeted validation.
2. **v0.2 NIS:** integrate NVIDIA's NIS shader SDK, validate input/output formats and output sizes, add sharpening and measured GPU cost. Use it as the default supported enhancement with unprocessed fallback.
3. **v0.3 diagnostics:** timestamp real and presented frames, report drops, pacing and processing cost. Label latency estimates separately from measurements.
4. **v0.4 experimental custom interpolation:** first validate NVIDIA Optical Flow SDK support on the selected adapter. Research a D3D11 compute or shader motion estimation and synthesis path for GTX 1080 Ti and other unsupported GPUs. Benchmark artifact rate, queue depth, latency, and pacing before exposing controls. Optical flow vectors alone do not create finished frames.
5. **Later native integration:** add renderer-specific interfaces for DLSS Super Resolution, Frame Generation, and Multi Frame Generation only when the renderer supplies required resources and Streamline reports support. Capture alone is insufficient.

## Sources

- [NVIDIA Image Scaling SDK requirements](https://developer.nvidia.com/rtx/image-scaling) and [source](https://github.com/NVIDIAGameWorks/NVIDIAImageScaling)
- [NVIDIA Optical Flow SDK requirements](https://developer.nvidia.com/opticalflow/download)
- [NVIDIA Streamline feature checks](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuide.md)
- [NVIDIA Streamline DLSS Frame Generation requirements](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md)
- [NVIDIA DLSS 4 model and GPU feature overview](https://www.nvidia.com/en-eu/geforce/news/dlss4-multi-frame-generation-ai-innovations/)
- [Microsoft Windows Graphics Capture for Win32](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createforwindow) and [frame pool guidance](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.direct3d11captureframepool)

See [CHANGELOG.md](CHANGELOG.md) for project changes.
