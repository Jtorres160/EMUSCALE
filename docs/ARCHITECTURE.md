# Architecture and capability contract

## Processing path

```text
Emulator window → Windows Graphics Capture → D3D11 texture → optional IUpscaler
                                                    → optional IFrameGenerator
                                                    → preview presenter
```

The v0.1.2 path presents captured frames directly. `WindowDiscovery` enumerates visible top-level windows. `WindowCapture` creates a free-threaded Windows Graphics Capture frame pool and retains only the latest frame under a mutex; its callback never presents. Notifications are coalesced around the mailbox so a stalled presenter cannot accumulate one UI message per capture callback. The Win32 preview thread extracts the D3D11 texture and draws it with an aspect-preserving shader into a flip-model swap chain. Direct shader-resource access is attempted first; one GPU copy is used only when the capture texture cannot be bound as a shader resource. Frame-pool buffers are recreated after content dimensions change. The capture item closure event ends the preview.

`FramePacer` applies numeric caps to newly delivered frames on the presentation thread; Auto and Unlocked add no software cap. `PreviewRenderer` selects synchronized or unsynchronized `Present` calls and checks DXGI tearing support before setting the swap-chain and present flags. `FrameMetrics` records callback, distinct compositor timestamp, and presentation events separately under a mutex. `Benchmark` samples those counters once per second and writes CSV. The Win32 settings window owns the controls; it shares the preview thread's message loop. A distinct compositor timestamp is not a texture-content comparison, and generated frame count stays zero.

Upscalers and frame generators are independent interfaces in `include/emuscale/graphics/Backends.h`. A failed optional processor returns to direct presentation, releases or resets its own resources, and records why it was disabled. Frame generation must never block capture indefinitely or grow an unbounded queue. Original frame timestamps and generated frame timestamps remain distinct.

## Independent diagnostics

`DiagnosticSession` owns a worker that samples shared `FrameMetrics` and scalar atomic settings once per second; disk and resource-query work never runs in the capture callback. Its bounded event queue carries configuration/activity/error markers. The worker progressively writes schema-versioned metadata, metrics CSV and event JSONL, then invokes the same saved-file analyzer used by `emuscale_analyze`. It joins at shutdown. The analyzer validates saved rows and distinguishes observations from possible causes; a missing session-end event marks a partial recording.

`DisplayInventory` queries desktop monitor bounds, DisplayConfig panel IDs/rational refresh rates, effective per-window DPI using hidden non-activating helper windows, and startup display modes. Capture targets an HWND, not a primary-display rectangle. Diagnostics select a single visible Eden process window with a declared-game title match; ambiguous/unknown titles require an explicit reviewed HWND. The preview starts on another monitor without activation. No emulator window-management or game-memory APIs are used.

Process usage uses limited-information handles and working-set queries. Optional Windows PDH counters report maximum engine utilization per process across adapters. Both unavailable counters and unmeasurable timestamp delay are explicit. Presentation metrics count successful Present returns, excluding occlusion, rather than claiming physical display FPS. Logging work is sampled one cycle late and cannot alone establish causal gameplay overhead. Storage accounting holds an exclusive file handle; 32 MiB per session and 256 MiB per directory stop logging without deleting old sessions or stopping capture. The original `Benchmark` remains the separate v0.1.1 timed CSV workflow.

## Runtime detection

`detect_gpus()` enumerates DXGI adapters and probes D3D11 creation on each adapter. The selected adapter must be used for capture, processing, and presentation or an explicit cross-adapter transfer must be measured. Each backend reports three independent states:

| State | Meaning | How it becomes supported |
|---|---|---|
| Hardware | Selected adapter and required GPU/API functions qualify | D3D feature checks and backend SDK adapter query |
| SDK | Required SDK and runtime loaded, with OS/driver requirements met | SDK initialization and feature requirement query |
| Integration | This application has a working data path | Build inclusion plus runtime initialization and processing validation |

`unknown` is distinct from `unsupported`; both disable selection. A successful hardware check alone never enables a feature. A marketing GPU series is useful UI context, not proof. Native DLSS features must use Streamline's selected-adapter feature query and requirement checks, then confirm runtime state. NVIDIA Optical Flow must probe that adapter before use. GTX 1080 Ti is outside the current Optical Flow SDK's Turing-or-newer requirement; research compute interpolation for it.

The backend policy chooses the requested upscaler when all three checks pass, otherwise NIS if it passes, otherwise direct presentation. For frame generation it disables the requested backend on failure; it never silently swaps official DLSS FG for custom interpolation. Conservative compatibility mode allows only verified NIS. The future UI should hide unavailable controls and show concise reasons for unknown/unsupported status.

## Native DLSS boundary

Native DLSS Super Resolution is a future renderer integration for eligible RTX GPUs, including RTX 20/30 where SDK-supported model and quality settings should be measured rather than hard-coded by GPU name. It requires renderer-produced temporal inputs; external capture does not recover valid motion vectors, depth, jitter, exposure, or reset state. Native DLSS Frame Generation is reserved for eligible RTX 40+ renderers and Multi Frame Generation for RTX 50+ renderers, subject to current SDK, OS, driver, and runtime state checks. These never share the custom interpolation implementation or its name.

## Frame pacing priority

Future interpolation should bound queue depth, reject repeated or irregular source frames, preserve presentation order, and disable itself when synthesis cost or artifacts exceed measured thresholds. Report source FPS separately from presentation FPS. Evaluate responsiveness and frame-time variance before advertising an FPS increase.
