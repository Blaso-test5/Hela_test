# GTX 950M v005 — converted pixel shader and t0 readback

Run date: 2026-11-11.

## Setup

- GPU: NVIDIA GeForce GTX 950M.
- The proxy used the v005 DLL pair with a separate versioned override
  directory. The game-root DLLs were restored to v001 after the run.
- The fullscreen-position VS override was retained. The two magenta PS
  overrides were replaced with bytecode compiled from the corresponding
  converted HLSL files in `shaders/hlsl/` using `ps_5_0`.
- Both converted PS overrides loaded as `SHADER READY`; no magenta PS was
  used in this run.
- Proxy hashes: `d3d12.dll`
  `2298DB2D4880A5B7366E0A0948278ACC1E274C7D1B5B5DA42B39F2DB12C09637`,
  `dxgi.dll`
  `FEBF8FA9DC6AB79DD5CEB852114938076ABE9556BC7894D54DE0E0F3F7B58DCC`.

## Verified result

- At target draw hit 1, t0 was bound as a 1x1
  `R8G8B8A8_UNORM` SRV over an `R8G8B8A8_TYPELESS` texture. The bounded
  staging readback returned `RGBA=(255,255,255,255)`.
- Present #100 matched a source history with 66 draws, zero skipped draws,
  and zero clears.
- The source and backbuffer readbacks are 1280x720, 24-bit, and each contains
  **0/921,600 non-black pixels**. Their SHA-256 hashes are identical:
  `D43EC90910E666B315661BE75DFEA0AF9722FF471AA30593999567DFFC53AC9C`.
- The PS constant buffers were readable and non-zero. The first target PS
  saw `(1, 0.454545438, 0, 1)` in b0; the second saw `(2.2, 1, 0, 1)`.

This rules out a black t0 as the explanation for the measured target draws.
The black full-frame result is not the game's visible scene.

## Evidence

- `d3d12on11-v005.log` — raw run log, SHA-256
  `78440C82BF0FEBAD43455C0DE4773CA5993A0F48E692A89FB09F2EA26FEFE362`.
- `live-monitor-report.json` — live monitor report.
- `live-monitor-postrun-report.json` — post-run BMP verification.
- `HelaReadback_Present100_source.bmp` and
  `HelaReadback_Present100_backbuffer.bmp` — full readbacks; identical hash
  listed above.
- `overrides/` — exact override bytecode used for this run.
- `fullscreen-position-vs.hlsl` — source for the test VS override.
