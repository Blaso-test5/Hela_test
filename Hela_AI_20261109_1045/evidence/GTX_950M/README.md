# GTX 950M Hela test

Run date: 2026-11-10

This directory contains only evidence from the Hela run on this PC's NVIDIA
GeForce GTX 950M (device `0x139A`, driver `582.78`, 4 GiB).

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
  CopyResource operations to the backbuffer. This instrumentation was added
  after this run; it compiled and passed the targeted smoke tests but has not
  yet been captured in a game trace.

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
- `Present100_source.bmp` and `Present100_backbuffer.bmp`: full 1280x720
  BMP readbacks from Present #100.
