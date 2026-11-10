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
