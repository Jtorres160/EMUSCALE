# v0.1.3 diagnostics and validation

## Build and automated checks

Run from a Visual Studio 2022 x64 developer environment:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The suite has 15 tests: capability policy, pacing/benchmark preservation, diagnostic writer/analyzer integration, timing/correlation/recovery policy, synthetic display geometry, WARP GPU processing, CLI help, B/C readiness guards, B optimizer guard, A limit guard, malformed duration, missing analysis session, reversed thresholds and non-finite thresholds. Test assertions remain enabled in Release. MSVC D9025 for overriding `NDEBUG` is expected and inherited from v0.1.2. No tests are intentionally skipped. An initial new geometry assertion used exact floating-point equality; it was corrected to a 1e-8 tolerance without changing the production calculation or weakening existing tests.

WARP tests use actual D3D11 textures/context/query objects without a display window. They verify resource identity/no-copy pass-through, SDR BGRA8 and RGBA8, explicit HDR rejection, invalid dimensions and foreign devices, output timestamp preservation, processor exception/invalid-output fallback, persistent processor disable, reset/destruction and completed GPU timing. Synthetic health/retry tests verify waiting, stalls, recovery, minimization, closure, replacement, 1/2/4 second retry delays and terminal device loss. These policy tests do not establish real WGC device-error recovery on this GPU.

## Diagnostic field definitions

Final MSVC x64 verification on 2026-11-01: Debug build passed and CTest **15/15** passed; Release build passed and CTest **15/15** passed. Final-suite failures: zero; skipped: zero. Existing tests were retained. No NIS SDK, Vulkan or CUDA dependency was added.

Schema 2 preserves the released v0.1.2 metrics prefix and adds fields. The analyzer accepts the released schema 1 header; intermediate development headers are not a public compatibility contract. Keep `metadata.json`, `metrics.csv`, `timings.csv`, `markers.csv` and `events.jsonl` together. The analyzer regenerates `summary.md` after a clean stop or when invoked manually. It reports invalid rows and partial sessions instead of silently treating them as valid benchmark data.

| Measurement | Definition / limit |
|---|---|
| Emulator simulation FPS | Unsupported from external capture; metadata uses null. No emulator timing, patches or speed controls are modified. |
| Callback FPS / total | WGC callbacks in the last second / cumulative callback count. A callback need not return an acquired frame. |
| Distinct capture FPS / total | Acquired frames with a new monotonic WGC timestamp. Not proof of pixel uniqueness or internal game FPS. Missing/repeated/backward timestamps are tracked separately. |
| `last_capture_qpc`, `last_source_timestamp_100ns` | Latest acquired frame arrival in QPC ticks and latest valid WGC timestamp in 100 ns units. The clock units must not be interchanged. |
| `present_calls_total` | Calls actually made to DXGI Present after rendering; no fill-frame/busy-loop scheduling. |
| `presented_fps`, `presented_total` | Present calls returning S_OK. Physical scanout/completion is not measured. Occluded/failed calls are counted separately; other nonfailing statuses may account for the remaining difference. |
| `new_source_present_total` | Accepted presents whose available source timestamp advances the presentation high-water mark. This is timestamp uniqueness only. |
| `repeated_present_total`, `unknown_source_present_total` | Accepted presents reusing/staling a known timestamp, or lacking a valid source timestamp. Neither is a generated game frame. |
| `generated_total` | Always zero in v0.1.3. |
| `capped_total`, `duplicate_skipped_total`, `minimized_skipped_total` | Acquired frames intentionally skipped by cap, duplicate/stale timestamp guard, or minimized-preview guard. These are distinct from mailbox overwrites. |
| `overwritten_total` | Frames replaced in the application's one-frame mailbox. It is not a count of every upstream/compositor loss. WGC uses two frame-pool buffers. |
| Timing fields in `metrics.csv` | Rolling ten-second interval statistics, sampled around once a second. Capture interval fields use acquired-frame arrivals; callback averages and source timestamp averages remain separate. |
| `timings.csv` | Acquired arrivals and accepted Present returns, with monotonic QPC, source timestamps, preceding interval and configured warning/spike thresholds. First intervals are zero because no predecessor exists. Records are serialized in worker batches; no per-frame disk writes occur on capture/presentation threads. |
| P50/P95/P99 | Linear interpolation at percentile positions `p * (N - 1)` over valid retained raw intervals. Session percentiles are not averages of rolling percentiles. Startup intervals are included; no automatic warmup exclusion is applied. |
| 33.3 / 50 ms thresholds | Configurable with `--warning-ms` / `--spike-ms`, finite and positive with warning <= spike. A 30 FPS cadence is 33.333 ms, so crossings of 33.3 ms are normal and not evidence of stuttering. |
| Delay estimate | WGC compositor timestamp to CPU Present-return time. Negative/out-of-range/missing correlations are unavailable, never zeroed into a latency average. Sparse valid samples do not support a session-wide latency estimate; no input/scanout latency is measured. |
| Resource use | Process CPU normalized over logical processors, working set MiB, optional PDH busiest GPU engine per process across adapters. -1 means unavailable. GPU counters do not measure NIS cost or total device utilization. |
| Processing timings | Last processing submission wall time in ms; last completed optional GPU query in ms and its sequence. No processor/GPU timing defaults to -1 for GPU time. No texture image readback is used. |
| Logging work / write time | Previous worker cycle's sampling/serialization/write wall time and write/flush time. The final worker cycle is not included. A matched diagnostics-disabled control is needed to attribute game overhead. |
| Timestamp/order/loss fields | Invalid monotonic ordering and overwritten timing-ring records are counted. The raw ring has 8,192 entries; interval histories are bounded to 16,384 entries and time windows. Event queue capacity is 128 with bounded UTF-8 labels. |

Events retain submission QPC/elapsed time; `utc` is write time. Multiple producers and automatic sampling may produce file lines out of occurrence order. Marker correlation uses occurrence times, not file order or UTC write time. Each anomaly is counted once per label within +/-2 seconds; different labels may describe the same anomaly. Capture and Present anomalies are counted separately. Counts alone do not establish statistical significance or a root cause. Automatic health/window/monitor markers may lag by about one sample.

All files share 32 MiB/session and 256 MiB/directory budgets under the exclusive recorder lock. Space is reserved for final events/analysis. Raw timing coverage can end early at a quota; summary/drop counters disclose that limit. Old files are never deleted automatically. Startup waits until output files/headers are writable; a failed recorder returns an error rather than silently claiming recording. Normal closure stops capture, drains telemetry, finalizes logs and analyzes; abrupt termination may leave a valid analyzable prefix. Source image/video and keyboard content are never recorded.

## Live record — 2026-11-01

Before changes, `main` was clean at `70844c9c288a8d91e5293270eef7e64b99ab8e18`. Process enumeration and a CIM executable/command-line query found no running `emuscale_rtx.exe`; PID 33312 was not assumed valid or terminated. Its saved session ended cleanly after 124.12 seconds: 30.68 timestamp-distinct capture and 30.64 accepted Present FPS, seven mailbox overwrites and 24 >50 ms intervals. Legacy logs cannot retrospectively supply exact frame-time percentiles.

The machine reports an RTX 4080 SUPER and two XV272U W2 displays at 2560×1440, 240.001 Hz, DPI 96 / 100%. Eden v0.2.1 with a TotK public title was on primary `DISPLAY1`; EmuScale chose negative-X secondary `DISPLAY2` for its preview. Eden was not moved, focused, resized, minimized or reconfigured by testing. No screenshots or gameplay-state manipulation were used. The game label is a user declaration; actual menu/gameplay activity was not independently classified.

A 30.08 second Debug smoke run exited 0 and reported 30.00 capture / 30.00 Present FPS with zero mailbox overwrites, stalls or >50 ms intervals. This was an intermediate development schema; the final Release validation below is the retained comparison artifact.

Release session `2026-11-01T21-39-44.954Z_22160` ran 121.09 seconds and exited 0. It intentionally mixed settings every approximately 20 seconds: Unlocked/Auto, Auto/Auto, 30/Auto, 60/Auto, Unlocked/Off, then Unlocked/On with an 800×600 outer preview resize (784×561 client). It is functional validation, not a matched-scene optimizer/VSync benchmark. NX Optimizer was left unchanged and its disabled state remains the user's declaration.

| Stream | Mean one-second FPS | Mean interval ms | Min ms | Max ms | P50 ms | P95 ms | P99 ms | >33.3 ms | >50 ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Capture arrivals | 31.34 | 31.914 | 13.348 | 100.309 | 33.330 | 33.824 | 36.193 | 2852 | 15 |
| Accepted Present returns | 30.99 | 32.267 | 1.735 | 100.342 | 33.330 | 33.833 | 36.604 | 2712 | 17 |

There were 122 valid metrics rows and zero invalid raw/marker rows; 3,792 acquired source timestamps, 3,750 accepted Present calls, 42 cap skips, no mailbox overwrites, no failed/occluded calls, no repeated/unknown presented timestamps, no observed capture interruptions, no order rejections, no raw-record drops and no generated frames. Process CPU averaged 0.06%, peaking at 0.53% over stable sample intervals. Worker work averaged 2.88 ms and peaked at 11.76 ms; flush peaked at 0.80 ms. A 30 FPS cap averaged 29.80 presented FPS from 31.75 timestamp-distinct captures; a 60 FPS cap averaged 31.50 from 31.65 captures. Raising the presentation cap did not manufacture game frames.

Within +/-2 seconds of the Auto-limit change there were five capture and five Present anomalies; near the resize/VSync-On change there was one of each. Overlapping labels can refer to the same frame interval. No human combat/camera/loading markers were supplied, so gameplay activity correlation is pending. Neither the setting changes nor EmuScale are established causes of those intervals.

Only 7 of 3,750 source-to-Present-return deltas were usable; 3,743 were negative/out of range. No session-wide latency estimate is supported. Investigate timestamp semantics/queue timing before interpreting the prior sparse rolling delay metric. Do not silently offset or clamp timestamps to manufacture a plausible latency.

## Manual scenarios still pending

The final fixed-setting Release baseline `2026-11-01T21-45-42.379Z_33744` ran 60.09 seconds with Unlocked/VSync Auto and exited 0. It had 61 valid metrics rows, no invalid timing/marker rows, and mean callback/distinct capture/Present rates of **31.97 / 31.97 / 31.97 FPS**. There were 1,918 acquired timestamps and accepted presents, zero >50 ms intervals, mailbox overwrites, capture interruptions, repeated/unknown presented timestamps, cap skips, order errors or record drops. Capture P50/P95/P99 were **33.331 / 33.395 / 33.928 ms**; Present P50/P95/P99 were **33.331 / 33.439 / 33.976 ms**. Maximum Present interval was **37.736 ms**. CPU averaged 0.06% (0.58% peak); worker work averaged 2.84 ms (11.29 ms peak), and flush peaked at 0.88 ms. There were 57 usable and 1,861 negative/out-of-range source-clock deltas, again insufficient latency coverage. No human activity markers were supplied.

The baseline and mixed-setting sessions used uncontrolled live activity, so the difference in long intervals does not prove an engineering improvement or a settings-induced fault. Timestamp-distinct capture rates above the declared 30 FPS target do not prove that TotK simulation ran above 30 FPS.

Start `emuscale_rtx --diagnostics --scenario=D --scene=<descriptive-label>` and keep the full session directory. The steps below are for the user to perform when ready; automation must not manipulate Eden to complete them.

| Scenario | Exact manual steps | Evidence to inspect | Status |
|---|---|---|---|
| Eden on secondary / movement both directions | Mark 9/10, drag Eden to Windows secondary, play the same scene 20–30 s, mark again and return it | Same PID/HWND, monitor assignments/refresh, dimensions, health and timing continuity | Pending |
| Independent preview movement | Drag only EmuScale's preview between monitors, leaving Eden fixed | Preview monitor changes while target identity/monitor stays fixed; aspect remains correct | Pending physically; secondary preview placement/resize verified |
| Mixed refresh/resolution/DPI | Manually configure different refresh/resolution/scaling in Windows, repeat a fixed scene and move only the preview | Display inventory, DPI events, current refresh and output geometry | Pending; synthetic pixel/coordinate/aspect policies passed |
| Display disconnect/reconnect | Mark 10, disconnect the preview display, reconnect it after 20 s | Topology event, preview visibility on remaining work area, capture continuity or explicit device error; no Eden movement | Pending; synthetic removed-display placement passed |
| Target minimize/restore | Mark 11, minimize Eden, wait 5 s, restore it yourself | Minimized/no-frame and recovery markers; no automatic restoration/focus | Pending; health policy tested |
| Temporary acquisition failure | Use a controlled Windows capture failure during a dedicated test, retaining HRESULTs | Original HWND retries at 1/2/4 s; stop after three failures or device loss, recover without settings changes | Pending live; health/retry policy tested |
| Closed/recreated target | Close Eden manually, reopen it, restart diagnostics and explicitly select the new target | Clean old-session stop and explicit selection; no silent capture of an unrelated/reused target | Pending in v0.1.3 |
| Gameplay correlation | Use marker buttons or `--mark=1..11` for actual exploration/camera/combat/loading/menu activity | Per-label capture/Present anomaly counts with occurrence times | Pending; synthetic correlation passed |
| Logging overhead | Repeat the same scene/config once with diagnostics, once without, including warmup | Matched frame pacing and process cost; do not infer causal overhead from worker wall time alone | Pending |

WGC source stalls may be normal for static content. Do not automatically restart capture based only on a missing-frame interval. Acquisition HRESULT failures use bounded recovery; device removal/reset requires restarting EmuScale. The target PID is rechecked and capture-item closure ends the session; no guessing among recreated windows is attempted. Full mixed-DPI controls usability, actual hardware hotplug, cross-adapter capture, HDR and long-duration device behavior are not claimed validated.

## v0.2 NIS sequence

1. Pin and review the official NIS SDK/license and add an optional processor factory, leaving capture/pass-through available without the SDK.
2. Verify SRV/UAV format support on the selected D3D11 adapter. Reuse GPU-only input/output textures; use an input GPU copy if WGC cannot supply an SRV. Keep output content/texture/request dimensions explicit.
3. Integrate NIS configuration/coefficients and its compute shader, validate supported scaling ratios and display-referred SDR color handling against [upstream guidance](https://github.com/NVIDIAGameWorks/NVIDIAImageScaling). No HDR claims until separately tested.
4. Recreate processor-owned resources safely on size changes and unbind compute resources before presentation. Validate reset/fallback under shader/format/allocation failure.
5. Compare pass-through and NIS in matched scenes using completed non-disjoint GPU queries, CPU submission cost, raw pacing and visual quality. Resolve source-clock coverage before making latency claims.
6. Complete the pending physical monitor/recovery matrix, then enable NIS only when capability, SDK and integration checks all pass. Keep emulator simulation controls, DLSS and frame generation outside this milestone.
