# GTX 950M v007 — white VS color counterfactual

Run date: 2026-11-11.

## Setup and identity

- The proxy DLLs were intentionally held at v006 because this test changed
  only the VS override, not proxy C++ code:
  - `d3d12.dll` SHA-256
    `DB3BE6F387B7BF534F89D20A50FF9DB280EA5CB1543DD1ED6D9C33CBC69EAF2B`.
  - `dxgi.dll` SHA-256
    `FEBF8FA9DC6AB79DD5CEB852114938076ABE9556BC7894D54DE0E0F3F7B58DCC`.
- The v007 VS was compiled from `fullscreen-position-vs.hlsl` using
  `vs_5_0`. Its exact bytecode SHA-256 is
  `0DD0F85D48D1E27852B99A696017A5DBBE84AAA4562271EDCF139D94AB8B39F8`.
- The two converted game PS overrides were kept unchanged:
  `44b0a992c2304254.cso` SHA-256
  `43C029B121212D0A37E0A00AAF983D9B6F7EA3EE593412C46EF22C52513D263D`;
  `d77600bde0076c33.cso` SHA-256
  `9F6FEE68B9C8C824F106426DB501012EBC176E35A22C7A8CD47E3E94719207B4`.
- The log confirms all three target overrides reached `SHADER READY`. It
  also confirms target t0 was white. The root DLLs were restored to v001
  after capture.

## Result

The VS override writes `COLOR=(1,1,1,1)` after decoding the captured vertex
color. Even with this intervention, the source and backbuffer readbacks at
Present #100 remained black: **0/921,600 non-black pixels** in each BMP.
Both BMPs have SHA-256
`D43EC90910E666B315661BE75DFEA0AF9722FF471AA30593999567DFFC53AC9C`.

This rules out black `COLOR` input as the sole explanation for the black
readback. It does not yet establish whether these draws reach the captured
source or whether another stage/state suppresses their output.

## Evidence

- `d3d12on11-v007.log` — raw log, SHA-256
  `9C3828CD45A5C983B7D06A4535F8D1BBBA5DAE700DB0650CB3CF542ED663FAA2`.
- `HelaReadback_Present100_source.bmp` and
  `HelaReadback_Present100_backbuffer.bmp` — full-frame readbacks.
- `overrides/` — exact VS and both target PS bytecodes used.
- `fullscreen-position-vs.hlsl` — v007 VS test source.

The old proxy does not log module or shader SHA-256 fingerprints. The
identities above were verified from the archived pre-run DLLs/override
bytecode and the `SHADER READY` records. Starting with v008, each run is
preflighted against a required manifest and the loaded modules and shader
overrides attest their SHA-256 values in the proxy log.
