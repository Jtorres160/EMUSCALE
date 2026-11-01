# Architecture and capability contract

## Processing path

```text
Emulator window → Windows Graphics Capture → D3D11 texture → optional IUpscaler
                                                    → optional IFrameGenerator
                                                    → preview presenter
```

The v0.1.3 path presents captured frames through an optional D3D11 processing boundary, with pass-through as the default. `WindowDiscovery` enumerates visible top-level windows. `WindowCapture` creates a free-threaded Windows Graphics Capture frame pool and retains only the latest frame under a mutex; its callback never presents. Notifications are coalesced around the mailbox so a stalled presenter cannot accumulate one UI message per capture callback. The Win32 preview thread extracts the D3D11 texture, processes it if a processor is installed, and draws with an aspect-preserving shader into a windowed flip-model swap chain. Direct shader-resource access is attempted first; one cached GPU copy is used when a captured texture cannot be bound as a shader resource. Frame leases close on early returns and exceptions. Frame-pool buffers are recreated after content dimensions change. Closing the capture item ends the preview; a new HWND requires explicit re-selection.

`FramePacer` applies numeric caps to newly delivered frames on the presentation thread; Auto and Unlocked add no software cap. `PreviewRenderer` selects synchronized or unsynchronized `Present` calls and checks DXGI tearing support before setting the swap-chain and present flags. `FrameMetrics` records callback, distinct compositor timestamp, and presentation events separately under a mutex. `Benchmark` samples those counters once per second and writes CSV. The Win32 settings window owns the controls; it shares the preview thread's message loop. A distinct compositor timestamp is not a texture-content comparison, and generated frame count stays zero.

Upscalers and frame generators are independent interfaces in `include/emuscale/graphics/Backends.h`. A failed optional processor returns to direct presentation, releases or resets its own resources, and records why it was disabled. Frame generation must never block capture indefinitely or grow an unbounded queue. Original frame timestamps and generated frame timestamps remain distinct.

## Independent diagnostics

Schema 2 adds a bounded 8,192-record timing buffer to `FrameMetrics`. Capture arrivals and successful Present returns append compact timing records under the existing mutex; no per-frame file operations or image readbacks are added. The diagnostic worker drains/serializes the buffer each second under the same session/directory budgets as metrics and events. Timing loss, timestamp-order rejection and source/presentation classifications are explicit counters. `TimingAnalysis` provides tested percentile, marker correlation, capture health and bounded retry policies. The saved-file analyzer accepts released schema 1 metrics as well as schema 2; legacy sessions cannot supply raw frame percentiles retroactively.

Marker occurrence uses QPC at submission, while event `utc` is write time. Asynchronous event lines may appear out of occurrence order, particularly where automatic sampling overlaps UI commands. Analysis uses occurrence times and correlates each anomaly once per marker label within +/-2 seconds. Capture and presentation anomaly counts stay separate. Capture health is sampled once a second; missing acquired frames do not distinguish a static source, minimized game, scheduling delay or capture fault. Actual acquisition failures retry the original valid HWND after 1/2/4 seconds, then fail cleanly. Device removal/reset is terminal and requires restart. No automatic target switching or emulator state modification is performed.

`DisplayPolicy` contains pure monitor intersection/nearest selection, negative desktop coordinate translation, aspect fitting and preview visibility rules. `WM_DPICHANGED` applies Windows' suggested rectangle only to the preview. `WM_DISPLAYCHANGE` refreshes diagnostic inventory and brings an offscreen EmuScale preview/settings window into a remaining work area without activation. Eden is never a window-management target. Real mixed-DPI/refresh/hotplug and recreated-target recovery still need the manual verification in `TESTING.md`.

`DiagnosticSession` owns a worker that samples shared `FrameMetrics` and scalar atomic settings once per second; disk and resource-query work never runs in the capture callback. Its bounded event queue carries configuration/activity/error markers. The worker progressively writes schema-versioned metadata, metrics CSV and event JSONL, then invokes the same saved-file analyzer used by `emuscale_analyze`. It joins at shutdown. The analyzer validates saved rows and distinguishes observations from possible causes; a missing session-end event marks a partial recording.

`DisplayInventory` queries desktop monitor bounds, DisplayConfig panel IDs/rational refresh rates, effective per-window DPI using hidden non-activating helper windows, and startup display modes. Capture targets an HWND, not a primary-display rectangle. Diagnostics select a single visible Eden process window with a declared-game title match; ambiguous/unknown titles require an explicit reviewed HWND. The preview starts on another monitor without activation. No emulator window-management or game-memory APIs are used.

Process usage uses limited-information handles and working-set queries. Optional Windows PDH counters report maximum engine utilization per process across adapters. Both unavailable counters and unmeasurable timestamp delay are explicit. Presentation metrics count successful Present returns, excluding occlusion, rather than claiming physical display FPS. Logging work is sampled one cycle late and cannot alone establish causal gameplay overhead. Storage accounting holds an exclusive file handle; 32 MiB per session and 256 MiB per directory stop logging without deleting old sessions or stopping capture. The original `Benchmark` remains the separate v0.1.1 timed CSV workflow.

## GPU processing boundary for v0.2

`GpuFrame` contains a COM texture reference, valid content pixel extent and the explicitly named WGC `source_timestamp_100ns`. This is distinct from the future capability contracts' QPC timestamp. A COM reference is not ownership of the WGC lease: neither an optional processor nor the preview may retain a captured source after that lease closes. `PreviewRenderer` releases processing results and transient SRVs before `show_frame` closes the lease. Pass-through returns the identical GPU resource and original content dimensions, while the presenter performs the existing final aspect fit.

`IGpuProcessor::process(device, immediate_context, source, requested_output)` is the NIS insertion point. It must treat input as read-only, allocate/reuse its own output, preserve the source timestamp, unbind its output resources and release/reset resources safely. The pipeline validates same-device ownership, SDR BGRA8/RGBA8 format, one mip/array/sample, GPU-default usage, content bounds and requested dimensions. Invalid output or a processor exception disables that processor, resets resources/context state and returns the untouched original resource. Invalid capture input remains an error rather than an invented conversion. The presenter establishes its graphics state after processing. The original future `IUpscaler`/`IFrameGenerator` contracts remain available; no native DLSS or generation implementation is introduced.

Processing submission wall time is measured on the CPU. Four reusable disjoint/timestamp query slots support optional GPU processing timing with a completion sequence; collection uses `D3D11_ASYNC_GETDATA_DONOTFLUSH`, skips pending slots and never waits/spins. Unavailable/disjoint timing is -1. Default pass-through issues no GPU processing queries or copies; its GPU processing time is unavailable, not a fabricated measured zero. WARP tests install a synthetic GPU-copy processor to verify output ownership, failure fallback and completed query timing.

NIS shader compilation, coefficient/config buffers, SRV/UAV format checks, resizing caches, supported scale ratios and color validation remain v0.2 work. Keep these inside an optional `IGpuProcessor` implementation/factory; failure must preserve pass-through. No NVIDIA SDK, CUDA or Vulkan is needed to build or run the v0.1.3 capture path. Follow the [official NIS integration and color guidance](https://github.com/NVIDIAGameWorks/NVIDIAImageScaling) and [D3D11 asynchronous query contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-getdata).

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
