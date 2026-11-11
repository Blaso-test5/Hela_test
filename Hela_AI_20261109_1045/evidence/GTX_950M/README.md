# GTX 950M Hela test

Run date: 2026-11-11

For the latest continuation state and next diagnostic run, see
[`checkpoint-20261111.md`](./checkpoint-20261111.md).

This directory contains only evidence from the Hela run on this PC's NVIDIA
GeForce GTX 950M (device `0x139A`, driver `582.78`, 4 GiB).

## Latest status — 2026-11-11

- v012's `Load` probe changed from encoded zero at Present #100 to encoded
  ones by #1300. These are diagnostic shader colors, not gameplay.
- v013's `SampleLevel` probe was encoded zero at #100. Its log stops at #800;
  no verified #1300 capture exists.
- v014's no-override baseline is uniformly black at #100; at #400 the
  matched RTV history reports 66 skipped draws. Real Hela rendering is still
  not demonstrated on this GPU.
- **v015 managed run:** Present #100 and #400 source/backbuffer BMPs are
  uniformly black (`0/921,600` non-black pixels). No SRV creation failure or
  `0x8007000e` was logged; 64 target descriptor writes were observed. This
  does not explain the earlier failure or demonstrate scene rendering: the
  final heartbeat has zero successful draws and 947 VS skips. See the
  [v015 run analysis](./runs/20261111/950m-v015-no-shader-override-srv-diagnostics-20261111-20261111-113122270/README.md).
- The manager verified v015's runtime fingerprints and restored the exact
  pre-run v001 DLLs and six installed shader overrides. Its run folder retains
  the log, report, four BMPs, manifest, and restore point. See the
  [checkpoint](./checkpoint-20261111.md) for the next diagnostic step.

## Result

- The first launch used the existing conservative setup. Hela exited with
  code 0 after about 13 seconds without creating the game window.
- The diagnostic run used the packaged proxy, diagnostic tracing, and the
  two corrected vertex-shader overrides. Hela showed Unreal's crash window
  and exited with code 3 after about 10 seconds.
- The proxy selected the GTX 950M, whose D3D11 device reports Tiled Resources
  Tier 1. Hela requested a 2 GiB reserved buffer; the proxy returned
  `E_NOTIMPL` because the reserved-resource path requires D3D11 Tier 2.
- No PSO, swapchain Present, draw, or backbuffer rendering was reached. The
  test does not establish black-screen rendering on this GPU.
- The game's original DLLs, INI, and shader overrides were restored after
  the run. The full crash context and minidump are not included; the summary
  omits machine/login identifiers, and the minidump can contain process
  memory.

## Tier 0 follow-up

- The proxy was forced to report D3D12 Tiled Resources Tier 0 while the
  native GTX 950M D3D11 device remained at Tier 1.
- Hela created a window titled `Hela` and remained alive for about 70
  seconds. The trace has no `CreateReservedResource` requests; the previous
  2 GiB reserved-buffer assertion was avoided.
- Final trace: `present=1025`, `PSO(G=983,C=647)`, `draw(ok)=0`, `bb=0`,
  `skip-other=1801`, `dispatch(ok)=0`, `placed(ok)=80`, `overlap=0`.
- The process exited with `0xC0000005` (access violation); no matching
  Unreal crash context identified the fault. The window and Presents were
  observed, but no backbuffer draw was recorded, so black-screen pixels
  were not independently confirmed.
- Trace review confirms both corrected VS overrides (`20e0826b236597a1`,
  `4ce363172cfd70be`) loaded successfully; `INPUT_LAYOUT_FAILED=0` and
  `PSO GRAPHICS FAILED=0`. The missing-VS hypothesis is false.
- All 1,801 skipped draws have `missing-vs=0`, `missing-shader=1` and
  account for `skip-other=1801`. Their missing PS hashes were
  `44b0a992c2304254` (1,024 draws), `d77600bde0076c33` (484), and
  `4c1bc8bf7e457acd` (293); each was logged as a placeholder, not ready.
- This first Tier 0 run did not include those PS overrides; the follow-up
  below records the targeted test and its result.
- Original DLLs, INI, binding map, and overrides were restored. Details and
  the full trace are in `tier0-test-summary.txt` and
  `d3d12on11-tier0-test.log`.

## Pixel-shader override follow-up

- Two initial launches used the conservative `shader_model=5_0` setting and
  exited before PSO creation; they are not valid rendering-test results.
  The controlled rerun used the exact diagnostic proxy binary and
  temporarily advertised Shader Model 6.7, matching the prior Tier 0 run.
- All three missing PS candidates loaded as SM5 overrides, alongside both
  corrected VS overrides. The trace recorded zero `DRAW_SKIPPED`,
  `INPUT_LAYOUT_FAILED`, and `PSO GRAPHICS FAILED` records.
- Final heartbeat: `present=1857`, `PSO(G=983,C=2158)`,
  `draw(ok)=2505`, `rtv=2505`, `bb=0`, `skip-other=0`,
  `dispatch(ok)=0`, `placed(ok)=179`, `overlap=0`. No
  `CreateReservedResource` request was recorded.
- The `Hela` window was observed for about 77 seconds; the process ended
  with `0xC0000005` after about 114 seconds total. The faulting module is
  unknown. Draws now execute, but none was classified as targeting the
  backbuffer; a visible rendered frame is still unverified.
- The original DLL, INI, and three installed overrides were restored.
  Details and the full trace are in `pixel-shader-test-summary.txt` and
  `d3d12on11-tier0-ps-overrides-test.log`.

## Backbuffer copy trace

- The instrumented DXGI path logged 7,057 successful `GetBuffer` wraps;
  each corresponding resource was flagged as a backbuffer. The native
  swapchain texture is 1536x864, format 24 (`R10G10B10A2_UNORM`).
- The run recorded 3,527 `CopyResource` operations targeting a backbuffer
  and 3,527 successful Presents. The first observed backbuffer copy
  immediately preceded a successful Present. This points to a copy/blit
  composite, not a draw directly to the swapchain buffer.
- `draw(ok)=3903` and `bb=0` are consistent with that route: no draw was
  classified as targeting the backbuffer. The process exited with code
  `16384` after about 135 seconds; its window was observed for about 103
  seconds.
- This confirms the proxy submits copies into the swapchain texture, but
  no pixel readback or screenshot was captured. A visible rendered image
  is therefore still unverified. Next, inspect the copy source and capture
  a bounded backbuffer readback before Present.
- DLLs, INI, and all staged overrides were restored. The raw 59 MB trace
  remains in the local test backup; only the sanitized summary is included
  in this evidence directory.

## Backbuffer readback

- Added opt-in readback before `Present` and `Present1` using
  `D3D12ON11_BACKBUFFER_READBACK=1`, sampled every 100th present. The
  sampler decodes `R10G10B10A2_UNORM` correctly and also supports common
  8-bit RGBA/BGRA formats.
- The GTX 950M run recorded 2,068 Presents and 2,068 `CopyResource`
  operations targeting the backbuffer. Twenty samples were captured, from
  Present #100 through #2000. All five reported points in every sample
  were `(R,G,B,A)=(0,0,0,0)`; all 100-point non-black counts were `0/100`.
- The window title was observed. The process exited with code `16384`
  (`0x4000`) after about 134 seconds. All game DLLs, INI settings, and
  staged overrides were restored and verified against their backups.
- This establishes that the sampled backbuffer pixels were zero at those
  points; it does not prove every pixel in every frame was black. The next
  diagnostic should sample the source texture of the backbuffer copy to
  distinguish empty source content from a copy-path problem. The next run
  should explicitly use windowed mode (`-windowed`).
- The 31 MB raw run log is retained locally at
  `D:\SteamLibrary\steamapps\common\Hela Demo\Hela\Binaries\Win64\D3D12On11Patch\backups\950m-backbuffer-readback-20261110-013458\backbuffer-readback-test.log`;
  see `backbuffer-readback-test-summary.txt` for the sanitized findings.

## Copy-source readback (initial attempt; window mode not verified)

- The run passed `-windowed`, but the UE5 `GameUserSettings.ini` still had
  `FullscreenMode=1` (borderless fullscreen). A visible Hela window title
  did not prove that the window was actually windowed; treat that launch
  mode as unverified.
- Source-resource tracking showed three textures, each 1536x864, format 24
  (`R10G10B10A2_UNORM`), and sampled them alongside the destination before
  Present. The run logged
  2,357 backbuffer-targeted `CopyResource` calls and 23 source/destination
  readback pairs (#100 through #2300). Every source sample had all five
  reported points at `(0,0,0,0)` and `0/100` non-black pixels; every
  destination sample was likewise zero.
- No source-query, staging-creation, Map, or format errors were recorded.
  The process exited naturally with `0x4000` after about 128 seconds.
  Original DLLs, INI, and overrides were restored and verified.
- The source and destination samples confirm zero-valued pixels at the
  sampled locations and times. They do not identify which upstream draw,
  clear, binding, or resource-lifetime issue caused this. Next, trace
  writes/clears to the three source resources and inspect the render-target
  bindings before their copies.
- Sanitized results are in `copy-source-readback-test-summary.txt`. The
  34.9 MB raw log remains local at
  `D:\SteamLibrary\steamapps\common\Hela Demo\Hela\Binaries\Win64\D3D12On11Patch\backups\950m-copy-source-readback-20261110-014625\copy-source-readback-test.log`.

## Verified windowed source readback

- Set UE5 `FullscreenMode`, `LastConfirmedFullscreenMode`, and
  `PreferredFullscreenMode` to `2` in the local user settings and launched
  with `-windowed -ResX=1280 -ResY=720`.
- Verified the actual HWND style was `0x14CF0000` (standard overlapped
  window). Its outer bounds were 1296x759 on a 1536x864 monitor, confirming
  it was not borderless fullscreen. The persistent UE5 config now remains
  in windowed mode at 1280x720.
- The run recorded 2,056 Presents and backbuffer copies, with 20 sampled
  source/destination pairs (#100 through #2000). Source and destination
  were 1280x720, format 24. All five points in all source samples were
  `(0,0,0,0)`, with `0/100` non-black pixels; destination samples were
  also zero.
- Hela exited naturally with `0x4000` after about 92 seconds. Proxy DLLs,
  proxy INI, and shader overrides were restored and verified.
- See `verified-windowed-source-readback-test-summary.txt`; the 31.2 MB
  trace remains local at
  `D:\SteamLibrary\steamapps\common\Hela Demo\Hela\Binaries\Win64\D3D12On11Patch\backups\950m-windowed-source-readback-20261110-015307\windowed-source-readback-test.log`.

## Red backbuffer injection and full readback

- Enabled `D3D12ON11_INJECTION_TEST=1` to clear the swapchain backbuffer to
  red after the proxy flush and immediately before the native Present.
  The run also enabled the sampled and full texture readbacks.
- Launched with `Hela -graphicsadapter=0 -windowed -ResX=1280 -ResY=720`.
  The live HWND style was `0x14CF0000` (`WS_OVERLAPPEDWINDOW`), with outer
  bounds 1296x759; this was a normal window rather than borderless fullscreen.
- The game completed 2,159 Presents and exited naturally with code 16384
  (`0x4000`) after about 97 seconds. Twenty-one source/destination samples
  were captured from Present #100 through #2100. Every source sample was
  black (`0/100` non-black pixels); every injected backbuffer sample was
  red (`100/100`).
- The full 1280x720, format-24 (`R10G10B10A2_UNORM`) BMPs from Present #100
  confirm the complete image contents: all 921,600 source pixels are black,
  and all 921,600 backbuffer pixels are red. The user had already confirmed
  the red window visually in the preceding injection run.
- This confirms the red clear reaches the backbuffer before Present and the
  swapchain can display the injected frame. The CopyResource source is
  already black, so the missing game image is upstream of the final
  backbuffer/Present step; the exact upstream cause remains unproven.
- The proxy DLLs and INI were restored and verified against their original
  SHA-256 hashes. UE5 settings remain windowed at 1280x720.
- `Present100_source.bmp` and `Present100_backbuffer.bmp` contain the full
  readbacks. The 28.6 MB raw trace remains local at
  `D:\SteamLibrary\steamapps\common\Hela Demo\Hela\Binaries\Win64\D3D12On11Patch\backups\950m-full-readback-20261110-022941\full-readback-test.log`.
- Added opt-in `[CMDLIST_SUMMARY]` counters for each submitted command list
  when diagnostic render tracing is enabled. The summary counts draw
  executions, skipped draws, executions with an RTV, backbuffer draws, and
  CopyResource operations to the backbuffer.

## Source-resource draw history with SM5 overrides

- Built and tested the source-resource history instrumentation together with
  the DXGI bridge. All six CTest tests passed with the GTX 950M Tier 0
  diagnostic setting.
- Ran `Hela-Win64-Shipping.exe` directly, without Steam, using
  `-graphicsadapter=0 -windowed -ResX=1280 -ResY=720`,
  `D3D12ON11_SHADER_MODEL=6_7`, diagnostic tracing, and Tier 0. Installed
  the matching Release DLL pair and the SM5 overrides for VS
  `20e0826b236597a1` and PS `44b0a992c2304254` / `d77600bde0076c33`.
- The window remained black. At Present #100, three CopyResource source
  resources reported 64, 64, and 66 operations; all were executed draws,
  with zero skipped draws and zero RTV clears. The first 20 retained
  entries per resource show alternating draws with the listed PS shaders,
  six elements, one instance, RTV slot 0, and viewport 1280x720.
- Final trace: `present=2478`, `draw(ok)=2853`, `skip-vs=204`,
  `skip-other=0`, `placed(ok)=334`, `overlap=6`. Hela exited with `0x4000`
  after about 132.6 seconds.
- This confirms execution of draw commands on the CopyResource source
  resources, not their pixel values. Pixel readback was not enabled in this
  run, so the black window does not prove these particular draws produced
  black pixels. A same-run readback with the overrides is still needed.
- The original game DLLs, INI, and installed overrides were restored and
  verified. The 34.1 MB raw trace remains local; see
  `source-history-test-summary.txt` for the sanitized findings.

## Files

- `d3d12on11-before.log`: baseline launch log from the conservative setup.
- `d3d12on11-950m-test.log`: complete proxy trace from the diagnostic run.
- `crash-summary.txt`: sanitized Unreal assertion summary.
- `d3d12on11-tier0-test.log`: complete trace from the forced Tier 0 run.
- `tier0-test-summary.txt`: setup, results, and interpretation of the Tier 0 run.
- `d3d12on11-tier0-ps-overrides-test.log`: full trace after staging the
  three missing PS overrides.
- `pixel-shader-test-summary.txt`: results and interpretation of the PS test.
- `backbuffer-copy-test-summary.txt`: sanitized GetBuffer, resource-wrap,
  backbuffer-copy, and Present counts from the instrumented run.
- `backbuffer-readback-test-summary.txt`: sampled backbuffer pixel values
  and the bounded GTX 950M readback test result.
- `copy-source-readback-test-summary.txt`: source and destination
  measurements from the initial follow-up run; actual window mode was not
  verified.
- `verified-windowed-source-readback-test-summary.txt`: results from the
  run whose window style and dimensions were explicitly verified.
- `full-red-backbuffer-readback-test-summary.txt`: results from the verified
  windowed red-injection run, including complete source and destination
  BMP analysis.
- `source-history-test-summary.txt`: results from the source-resource
  history run with the three target SM5 overrides loaded.
- `Present100_source.bmp` and `Present100_backbuffer.bmp`: full 1280x720
  BMP readbacks from Present #100.

## November 10 raw run artifacts

- The non-empty `.log`, `.json`, and `.txt` outputs from the November 10
  diagnostic runs are archived under `runs/20261110/`, preserving their
  original backup-directory names. This includes the early Tier 0,
  backbuffer/source readback and injection runs, the live-monitor attempt,
  and the v001/v002/v003 override runs.
- The archive contains 49 files (about 496 MB uncompressed). Copied
  `previous-*` and `pre-run-*` log snapshots and empty stderr files are
  omitted; the primary run logs, monitor output, and JSON reports are kept.
- The v003 parser regression was verified against the archived v003 log:
  Present #100's source history has 66 draws, zero skipped draws, and the
  target VS/PS hashes. Source and backbuffer readbacks remain black, so the
  rendering cause is still undetermined.

## November 11 real-pixel-shader runs

- **v005:** both converted game PS shaders loaded as SM5 overrides with the
  fullscreen VS test override. The t0 texel was white
  (`RGBA=(255,255,255,255)`), but source and backbuffer Present #100 BMPs were
  fully black (`0/921,600` non-black pixels). This rules out a black t0 for
  the measured draws.
- **v006:** the target input layout and first four vertices were read back.
  Their `ATTRIBUTE3` colors are black RGB/alpha 1, and both target PS shaders
  consume that color for RGB. The Present #100 history contains only the
  target VS and those two PS hashes (66 draws, no skips or clears).
- The v006 heartbeat reached 465 Presents with 2,126 compute PSOs created and
  zero compute dispatches executed. The measured quad's output is explained,
  but the missing scene is not. Full setup, interpretation, hashes, raw logs,
  reports, and BMPs are archived under
  [`runs/20261111`](./runs/20261111/).
- v005/v006 restored the game directory to v001 and verified its DLL hashes.
- **v007:** the white-`COLOR` VS override was confirmed loaded with both
  converted PS overrides and white t0, but Present #100 source/backbuffer
  stayed entirely black. Black vertex colors alone are not sufficient to
  explain the readback; see
  [`950m-v007-white-vertex-color-20261111`](./runs/20261111/950m-v007-white-vertex-color-20261111/README.md).
- New v008 tooling now requires a run manifest that pins both proxy DLLs and
  the exact full `.cso` override set before launching. The proxy also logs
  the SHA-256 of loaded modules and shader overrides; the launcher rejects
  missing or mismatched runtime attestations.
- **v008:** with both target PSs returning solid white, the source and
  backbuffer readbacks became entirely white (`921,600/921,600` non-black).
  Runtime DLL and all required shader fingerprints matched the run manifest
  both before and after the test. This proves the controlled draw/copy path
  works and narrows the remaining black output to the converted PS logic or
  its inputs/translation; it is not normal game rendering. Details and all
  hashes are in
  [`950m-v008-solid-ps-fingerprint-20261111`](./runs/20261111/950m-v008-solid-ps-fingerprint-20261111/README.md).
- **v009:** both target PSs returned sampled t0 RGB multiplied by the
  interpolated VS `COLOR`, with gamma and constant-buffer adjustments
  bypassed. The white t0 probe and pipeline statistics confirm the target
  draws execute on the captured source resource (921,600 and 230,400 PS
  invocations). Both Present #100 readbacks nevertheless remained entirely
  black. The actual per-pixel PS sample and interpolated inputs are still
  unknown; the next test will emit sampled t0, `COLOR`, and their product in
  separate channels. Evidence and hashes are archived in
  [`950m-v009-sample-color-fingerprint-20261111`](./runs/20261111/950m-v009-sample-color-fingerprint-20261111/README.md).
