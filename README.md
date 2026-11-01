# EmuScale RTX

EmuScale RTX is a Windows emulator enhancement project for NVIDIA GTX 900, GTX 10 (including GTX 1080 Ti), GTX 16, RTX 20, RTX 30, RTX 40, and RTX 50 series GPUs. Each enhancement is conditional on its actual hardware, driver, OS, SDK, and application requirements. GPU branding alone does not enable a feature.

## Current state

The project contains a C++20 capability library, Windows DXGI/D3D11 GPU enumeration, a GPU information console tool, independent future backend interfaces, a conservative selection policy, and tests. The v0.1.2 executable preserves the v0.1.1 emulator window selector, Windows Graphics Capture, D3D11 preview, FPS controls, and CSV benchmarks, and adds dual-monitor-aware Eden/TotK diagnostics, continuous local logs, activity markers, and a native session analyzer. NIS processing, DLSS, and frame generation remain disabled. See [the architecture](docs/ARCHITECTURE.md).

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

## Build and run v0.1.2

Install Visual Studio 2022 with **Desktop development with C++**, the MSVC x64 toolset, a Windows 10/11 SDK version 19041 or newer (including C++/WinRT headers), and CMake tools. Windows 10 version 1903 or newer is required for window capture; Windows 11 is supported. In a Developer PowerShell for VS 2022:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Launch Eden, leave its game window visible and restored, then run `build\Debug\emuscale_rtx.exe`. Select the numbered Eden window from the console list. The preview and a separate settings window open. The settings window can be hidden and reopened with F1 while the preview is focused. Close the preview to stop; restarting the program is required to choose a different target. Other visible top-level application windows can also be selected. Run `build\Debug\emuscale_gpu_info.exe` to inspect detected adapters and independent capability states; add `--compatibility=conservative` for compatibility mode (`automatic` is the default).

FPS Limit offers Auto, 30, 60, 90, 120, 144, 165, 240, and Unlocked. Auto is the default: it does not add a software cap, and the displayed target is the preview monitor's refresh rate. Unlocked also has no software cap and displays its target as Unlocked. The numeric choices cap presentation of new capture frames; they cannot create extra source frames or alter emulator speed. VSync Auto and On use synchronized presentation. Off requests unsynchronized presentation and uses DXGI tearing only when the system reports support. It may visibly tear. The renderer does not repeatedly present the same captured frame to fill a higher target FPS.

The settings panel reports capture callbacks, distinct Windows Graphics Capture timestamps, presented FPS, separate average source timestamp/callback/presentation intervals, 1% low FPS (after enough samples), pacing variation, overwritten mailbox frames, repeated timestamps, capped frames, and generated frames (always zero in v0.1.1). A distinct capture timestamp is **not** proof that the underlying game image changed. These are capture and presentation measurements, not emulator internal FPS or input latency. The 1% low and pacing statistics use up to ten seconds of presentation intervals; the FPS counters use a one second window. Mailbox overwrites count only frames replaced inside this application. The diagnostic line identifies an active numeric cap or a possible preview/capture rate mismatch; it cannot distinguish an emulator limit from a game limit or prove a GPU processing bottleneck. Raising a game's own FPS may require external, game-specific configuration. Windows Graphics Capture cannot universally unlock emulator or game FPS.

Benchmark mode supports 30, 60, or 120 seconds. Start Benchmark records one metrics row per second and automatically saves a CSV to `build\Debug\benchmarks` when time expires. Stop and Export CSV ends a run early; closing the preview also exports an active run. The CSV includes elapsed time, distinct timestamp FPS, callback FPS, presented FPS, target FPS, separate timing averages, 1% low, pacing deviation, cumulative drop/repeat/cap counts, GPU, dimensions, and VSync setting. Benchmarking observes the capture path only; it does not control Eden or gameplay timing.

The preview is SDR BGRA8. Protected content, minimized targets, inaccessible windows, and capture-disabled systems may produce no frames or an error. The program makes no claim about end-to-end input latency or GPU processing cost. An overwritten frame count measures frames replaced in the application's one-frame mailbox; it is not a count of every frame lost upstream. Qt 6 and NVIDIA SDKs are not required for v0.1. The build requires Windows; non-Windows configuration fails intentionally.

## Eden / TotK diagnostics

The baseline environment is Windows, an NVIDIA RTX 40-series GPU, Eden running **The Legend of Zelda: Tears of the Kingdom (TotK)**, NX Optimizer disabled by the user, an expected game target of roughly 30 FPS, and an Unlocked EmuScale limit. Expected game FPS is a declaration, never a measured result. Diagnostics do not change Eden, NX Optimizer, game speed, or display settings. The TotK label comes from the user's declaration; EmuScale does not read game memory to identify it.

Launch Eden and play normally, then run:

```powershell
# Continuous recording until you close EmuScale, stop it, or Eden closes.
.\build\Debug\emuscale_rtx.exe --diagnostics

# Test A: a 60-second baseline; use 120 for the longer run.
.\build\Debug\emuscale_rtx.exe --diagnostics --duration=60 --scene=exploration-route
```

Diagnostics automatically select a single visible, restored Eden process window whose public title mentions TotK or Tears of the Kingdom. If several plausible Eden windows exist, or the title cannot confirm the declared game, capture stops before selection: run `--list-eden`, review the process, title, HWND and monitor, then explicitly confirm with `--target-hwnd=<HWND>`. This is title matching, not proof of emulator-internal game state. Other windows are never silently substituted. The original manual window selector remains available by running without arguments. Closed, hidden and minimized targets are not restored automatically.

The capture target is the selected HWND, regardless of whether it is on the primary or secondary monitor, including negative desktop coordinates. EmuScale places **its own preview** on another active display when available and shows it without activation. Its settings panel starts hidden in diagnostics; F1 with the preview focused opens the panel. EmuScale does not move, resize, minimize or focus Eden. Monitor IDs, panel names, resolution, rational refresh rate, effective DPI/scale, desktop position, primary flag, active count and available modes are saved at startup. Target and preview monitor assignments/refresh rates are sampled every second; display inventory is refreshed every five seconds. Run `--display-info` for an inventory without starting capture. An unavailable DPI is reported as zero rather than invented.

Recording lives in the executable directory's `diagnostics/<UTC timestamp>_<PID>/`:

| File | Contents |
|---|---|
| `metadata.json` | Version, declared game/scenario/optimizer/scene, target process/PID/HWND/title, GPU, display inventory, limits and metric definitions |
| `metrics.csv` | Approximately one-second samples of capture callbacks, timestamp-distinct and successful Present rates/cumulative totals, timing averages, 1% low, pacing variation/max interval/spike count, compositor-to-Present-return delay, mailbox overwrites, repeats, caps, dimensions, limit/VSync/tearing policy, monitor IDs/refresh, process CPU/memory, GPU engine counters, logging work/write time, sampling gaps and dropped diagnostic events |
| `events.jsonl` | Progressive startup, configuration/display changes, activity markers, capture stop/error details and session-end events |
| `summary.md` | Automatically generated validation, measured observations, suspected causes and next engineering steps |

Sampling and file writes run on a dedicated worker, independently of the preview message loop and Codex. No screenshots, recorded video, GPU readback for content comparison, global keyboard hooks, or keyboard-content collection are used. Events have a 128-entry queue and bounded labels; frame statistics retain bounded histories. Logs flush progressively each second, so partial files remain analyzable after a forced exit. A session is limited to 32 MiB, and all sessions in that directory to 256 MiB. Existing logs are never deleted automatically. An exclusive recorder lock prevents simultaneous writers from bypassing directory accounting. At quota exhaustion logging ends and capture continues; archive logs before starting another recording. Disk/open failures are reported in the console/settings panel; a failed startup does not silently start an unlogged diagnostic capture.

`presented_fps` counts successful `DXGI Present` calls, excluding occluded returns; it does not measure physical scanout. Delay measures the WGC compositor timestamp to Present return using the QPC clock, not input or physical display latency; `-1` means no valid recent correlated samples. Rolling timing statistics cover up to ten seconds. Spikes are presentation intervals **over 50 ms**, counted cumulatively; game loading or an intentional pause can produce them. Timestamp-distinct FPS is not visually changed FPS, and pixel duplicates remain unmeasured. CPU percentages are normalized over all logical processors; memory is process working set. GPU workload uses Windows PDH GPU Engine counters and reports the busiest engine for each process across adapters, not total GPU utilization. Optional resource metrics use `-1` when unavailable. Diagnostic work includes sampling/serialization/write/flush on the worker and is recorded one cycle late; a matched run with diagnostics disabled is needed to measure causal gameplay overhead. Final intervals under 0.5 seconds remain in the raw log for cumulative counters but are excluded from mean FPS and CPU summaries to avoid duplicate rolling windows and coarse process-time rounding at shutdown.

Use marker buttons in the settings panel, or send a command from another console. The command accepts only one of seven predefined marker numbers:

```powershell
.\build\Debug\emuscale_rtx.exe --mark=1   # Normal exploration
# 2 Combat; 3 Fast travel; 4 Menu opened; 5 Loading; 6 Effects; 7 Stutter observed
.\build\Debug\emuscale_rtx.exe --stop     # Stop EmuScale capture and finalize logs

# Analyze saved data again, including partial sessions. No Python install is needed.
.\build\Debug\emuscale_analyze.exe '.\build\Debug\diagnostics\<session>'
```

Commands refuse to guess if several EmuScale previews are running. Normal preview closure, timed completion, Eden closure and capture errors finalize logs and generate a summary. A crash/forced termination may omit the end event; the analyzer identifies that as a partial session. It validates the CSV schema, row shapes, finite/nonnegative values, cumulative counters and time ordering. It reports confirmed observations separately from suspected causes, checks presentation tracking, interruptions, logging work and configuration changes, and explicitly marks content-duplicate detection unavailable. Preserve metadata and events alongside the CSV when sharing results. Window titles, process IDs and hardware IDs may identify your local environment.

### Benchmark scenarios

| Scenario | User preparation | EmuScale invocation |
|---|---|---|
| A — Baseline | NX Optimizer disabled; play a repeatable scene | `--diagnostics --duration=60 --scene=route` (Unlocked, VSync Auto) |
| B — Higher FPS | Explicitly confirm a known working optimizer configuration and correct game speed | `--diagnostics --scenario=B --ready --optimizer=known-working-config --scene=route --duration=60` |
| C — Presentation comparison | Explicitly confirm readiness; preserve the same scene and optimizer settings for every run | `--diagnostics --scenario=C --ready --optimizer=disabled --scene=route --duration=60 --vsync=Auto` |

Run B and C **only after the user declares readiness**. B/C refuse to start without `--ready`, an explicit optimizer label and a scene label. B rejects the label `disabled`. C can use a disabled or enabled declared optimizer configuration; repeat its command with `--vsync=On` and `--vsync=Off` after readiness, without changing the scene or optimizer. EmuScale records declarations and cannot verify correct game speed or whether the declared optimizer is actually enabled. Allowed timed durations are 30 (legacy short test), 60 or 120 seconds; omit the duration or use zero for continuous recording. Numeric `--limit=30|60|90|120|144|165|240`, `Auto`, and `Unlocked` are supported; baseline A requires Unlocked. Controls still permit manual changes, which appear in events and per-sample settings; avoid mixing settings inside comparison runs.

### Validation record — 2026-11-01

v0.1.2: MSVC x64 Debug and Release builds passed. CTest passed **10/10** in each configuration, covering capability/pacing policies, timing gaps/missing timestamps, progressive diagnostics, markers, recorder exclusion, quota exhaustion, partial/malformed sessions and CLI readiness guards. Test assertions remain active in Release; MSVC emits D9025 when `/UNDEBUG` overrides the configuration's `/DNDEBUG` for test targets.

Two live baseline captures completed with Eden v0.2.1 / TotK on an RTX 4080 SUPER. The Release session `2026-11-01T21-16-00.935Z_10712` exited with code 0, recorded 61.01 seconds with 61 valid rows and no invalid rows, and averaged **30.66 callback / 30.66 timestamp-distinct capture / 30.66 presented FPS** (sampled range 25–32). The selected game window was on Windows `DISPLAY1` (primary); the preview was on `DISPLAY2` at negative desktop X. Both panels reported 2560×1440, 240.001 Hz and 100% DPI scale. This validates capture across two displays in that actual placement; capture with Eden on the Windows-secondary display and mixed-DPI/hotplug behavior still need live validation. Eden was not moved for testing. The startup audit observed unchanged target bounds and foreground window.

The Release run captured 2560×1440 into a 984×611 preview with Unlocked presentation and VSync Auto. It recorded zero mailbox overwrites, timestamp repeats, empty callback windows, sampling gaps over 1.5 seconds, setting changes, or dropped diagnostic events. Fifteen presentation intervals exceeded 50 ms; the maximum was 108.32 ms. The largest sampled rolling compositor-to-Present-return mean was 19.35 ms. Process CPU averaged 0.05% over all logical processors (0.62% peak); the working set ranged 74.19–75.29 MiB. Logged worker work averaged 2.78 ms per sampled cycle (11.10 ms peak); write/flush peaked at 0.28 ms. GPU engine counters were available. NX Optimizer being disabled was the user's baseline declaration, not independently verified.

Capture and presentation tracked closely, so these measurements do not establish a capture bottleneck. Next, correlate the >50 ms intervals with loading/travel/stutter markers, repeat a fixed scene, and use a matched diagnostics-disabled control before attributing overhead. The occasional >10 ms worker cycle warrants profiling display inventory/PDH sampling if a controlled comparison shows a gameplay impact. B/C were not run because their configurations were not declared ready. No texture-content uniqueness, physical scanout, internal game FPS or input-latency claim follows from these captures.

Earlier v0.1 validation:

On Windows with an NVIDIA GeForce RTX 4080 SUPER, the x64 Debug build passed and CTest passed (1/1). Eden v0.2.1 was detected with The Legend of Zelda: Tears of the Kingdom running. The preview showed changing gameplay frames at roughly 27–32 captured FPS and 27–32 presented FPS, with zero application mailbox overwrites in the observed samples. Resizing the preview from 1264×721 to 757×721 kept the image visible and aspect-correct. Changing Eden's capture dimensions from 2560×1440 to 1922×1157 did not interrupt capture. After Eden was closed through its normal confirmation dialog, the preview disappeared and `emuscale_rtx.exe` exited with code 0. This single session does not establish long-term leak or performance behavior on other GPUs and emulators.

## Next development milestones

1. **v0.1 capture:** Eden selection, real preview frames, aspect-preserving resize, changing capture dimensions, source closure, and clean process exit were observed on one Windows system. Device removal and long-duration resource use still need targeted validation.
2. **v0.1.1 FPS controls and benchmark:** numeric presentation caps, VSync choice, separate capture and presentation metrics, and CSV export. Texture-content uniqueness, end-to-end latency, and long-duration performance still require validation.
   **v0.1.2 diagnostics:** independent Eden/TotK logging, multi-display inventory, resource sampling, markers, bounded storage and native session analysis are implemented. Mixed-DPI/hotplug, target-on-secondary live capture, forced device removal and long-duration overhead remain validation work.
3. **v0.2 NIS:** integrate NVIDIA's NIS shader SDK, validate input/output formats and output sizes, add sharpening and measured GPU cost. Use it as the default supported enhancement with unprocessed fallback.
4. **v0.4 experimental custom interpolation:** first validate NVIDIA Optical Flow SDK support on the selected adapter. Research a D3D11 compute or shader motion estimation and synthesis path for GTX 1080 Ti and other unsupported GPUs. Benchmark artifact rate, queue depth, latency, and pacing before exposing controls. Optical flow vectors alone do not create finished frames.
5. **Later native integration:** add renderer-specific interfaces for DLSS Super Resolution, Frame Generation, and Multi Frame Generation only when the renderer supplies required resources and Streamline reports support. Capture alone is insufficient.

## Sources

- [NVIDIA Image Scaling SDK requirements](https://developer.nvidia.com/rtx/image-scaling) and [source](https://github.com/NVIDIAGameWorks/NVIDIAImageScaling)
- [NVIDIA Optical Flow SDK requirements](https://developer.nvidia.com/opticalflow/download)
- [NVIDIA Streamline feature checks](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuide.md)
- [NVIDIA Streamline DLSS Frame Generation requirements](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md)
- [NVIDIA DLSS 4 model and GPU feature overview](https://www.nvidia.com/en-eu/geforce/news/dlss4-multi-frame-generation-ai-innovations/)
- [Microsoft Windows Graphics Capture for Win32](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createforwindow) and [frame pool guidance](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.direct3d11captureframepool)
- [WGC compositor QPC timestamp](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.direct3d11captureframe.systemrelativetime), [PDH wildcard counter arrays](https://learn.microsoft.com/en-us/windows/win32/api/pdh/nf-pdh-pdhgetformattedcounterarrayw), and [per-window DPI](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getdpiforwindow)

See [CHANGELOG.md](CHANGELOG.md) for project changes.
