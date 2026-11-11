# GTX 950M v006 — target vertex input probe

Run date: 2026-11-11.

## Setup

- GPU: NVIDIA GeForce GTX 950M.
- This run added target-only input-layout logging and a 176-byte readback
  covering the four vertices used by the target quad. It also logs static
  blend factors.
- It reused the v005 converted PS overrides and fullscreen-position VS
  override. Both target PS overrides loaded successfully.
- Proxy hashes: `d3d12.dll`
  `DB3BE6F387B7BF534F89D20A50FF9DB280EA5CB1543DD1ED6D9C33CBC69EAF2B`,
  `dxgi.dll`
  `FEBF8FA9DC6AB79DD5CEB852114938076ABE9556BC7894D54DE0E0F3F7B58DCC`.
- The game-root DLLs were restored to v001 and Hela was closed after capture.

## Verified result

The target input layout was:

| Semantic | Format | Offset |
|---|---|---:|
| ATTRIBUTE0 | `R32G32B32A32_FLOAT` (2) | 0 |
| ATTRIBUTE1 | `R32G32_FLOAT` (16) | 16 |
| ATTRIBUTE2 | `R32G32_FLOAT` (16) | 24 |
| ATTRIBUTE3 | `B8G8R8A8_UNORM` (87) | 32 |
| ATTRIBUTE4 | `B8G8R8A8_UNORM` (87) | 36 |
| ATTRIBUTE5 | `R16G16_UINT` (36) | 40 |

All four captured vertices decode `ATTRIBUTE3` and `ATTRIBUTE4` as
`RGBA=(0,0,0,1)`. The converted target PS reads `COLOR` (`ATTRIBUTE3`) for
RGB; with this vertex color and the white t0 texel from v005, black RGB is the
expected output. The first draw blends with source-alpha/one-minus-source-
alpha; its alpha is 1, so that blend state does not explain the black RGB.

At Present #100, the matched source history has 66 executed draws, zero
skips, and zero clears. Its only VS hash is `20e0826b236597a1`; its only PS
hashes are `44b0a992c2304254` and `d77600bde0076c33`. The source and
backbuffer BMPs are both 1280x720, 24-bit, and contain **0/921,600
non-black pixels**. Both hashes are
`D43EC90910E666B315661BE75DFEA0AF9722FF471AA30593999567DFFC53AC9C`.

By the last captured heartbeat (28 seconds after startup), the proxy had
created 2,126 compute PSOs and reached 465 Presents, but had executed no
compute dispatches. The report lists 256 unique shader placeholders
(2,878 placeholder events); this does not prove those shaders were dispatched.
The measured quad is explained, but the absence of the game's scene is not.

## Evidence

- `d3d12on11-v006.log` — raw run log, SHA-256
  `5C845BA9F704C4BD390EA12D5DDCD0359383782D80C9FCD4A86194F0BB0CB09C`.
- `live-monitor-report.json` — live monitor report.
- `live-monitor-postrun-report.json` — post-run BMP verification.
- `HelaReadback_Present100_source.bmp` and
  `HelaReadback_Present100_backbuffer.bmp` — full readbacks; identical hash
  listed above.
- The exact target override bytecode is archived in the adjacent
  [`v005 run`](../950m-v005-t0-real-ps-20261111/README.md).
