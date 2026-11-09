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

## Files

- `d3d12on11-before.log`: baseline launch log from the conservative setup.
- `d3d12on11-950m-test.log`: complete proxy trace from the diagnostic run.
- `crash-summary.txt`: sanitized Unreal assertion summary.
