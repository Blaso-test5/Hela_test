# GTX 950M — v004 target draw-state run

Run date: 2026-11-11, approximately 09:26 local time.

## Setup

- GPU: NVIDIA GeForce GTX 950M.
- Launched `Hela-Win64-Shipping.exe` directly at 1280x720 with the v004
  proxy pair and v003 fullscreen/magenta shader overrides.
- `d3d12.dll` SHA-256:
  `76AF76317A089CE153AEB101BEDDD48C9F5F3C4EDFDDD112757ACDFDB8D2DB4F`
- `dxgi.dll` SHA-256:
  `FEBF8FA9DC6AB79DD5CEB852114938076ABE9556BC7894D54DE0E0F3F7B58DCC`
- Hela exited; the game directory was restored to the v001 DLL pair. No
  game process remains running.

## Verified result

At Present #100, both the `CopyResource` source and swapchain backbuffer
readbacks contain exactly **921,600/921,600 magenta pixels**
(`RGB(255,0,255)`) at 1280x720. The BMP files have identical SHA-256
`105AE3CD563AF6AF8271B3952FF5637939CE5104F36091EC334EA43BF869622C`.
The source history for the readback-matched resource records 66 executed
draws, zero skipped draws, and zero clears.

This is the first verified non-black full-frame output on the GTX 950M, but
it is produced by forced test shaders; it is **not yet the game's normal
visual output**.

The target diagnostics show:

- The readback-matched RTV resource is `000002524713D638`; target hits 5 and
  6 use that same resource. The `previous_copy_source` field in a draw
  record refers to the preceding copy sequence, so its different pointer is
  expected while the swapchain rotates buffers.
- The target VS/PS pipeline reports 6 IA vertices, 2 primitives, 4 VS
  invocations, and PS invocation counts of 921,600 and 230,400 for the two
  test PS variants.
- The index buffer's CPU shadow and D3D11 readback both contain
  `[0,1,2,2,1,3]`. The PS has t0 bound to a 1x1 `R8G8B8A8_UNORM` view and a
  sampler; the texel's contents are still unknown.
- The captured D3D11 state has a full 1280x720 viewport. The two PS variants
  use different scissors; depth testing is disabled.

## Evidence files

- `d3d12on11-v004.log` — full proxy log (20,820,411 bytes), SHA-256
  `74A61053965DA89B8500A729512C6580B0FB73B922B5284FB51F73FC035B357C`.
- `live-monitor-report.json` — post-run parser report, SHA-256
  `6286E5D6F7747324FFDA2851A360A77B6B4DBBE07E7895261B7DDD44B1228352`.
- `HelaReadback_Present100_source.bmp` — full source readback.
- `HelaReadback_Present100_backbuffer.bmp` — full backbuffer readback.

The next discriminating measurement is the actual t0 texel value while
running the real pixel shader, with only the fullscreen VS override retained.
