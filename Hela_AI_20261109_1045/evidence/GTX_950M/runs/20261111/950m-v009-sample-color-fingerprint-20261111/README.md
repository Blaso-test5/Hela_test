# GTX 950M v009 - sampled texel times VS color

Run date: 2026-11-11.

## Setup and identity

The manifest-gated launcher verified the v008 proxy pair and the complete
six-file override set before launch. The log attests the same DLL hashes and
the exact loaded target VS/PS hashes. Both target PS overrides were compiled
as `ps_5_0` from the `*.sample-color.hlsl` files in this directory; each
returns the sampled t0 RGB multiplied by the interpolated `COLOR`, with
alpha forced to 1. The v007 fullscreen VS, which writes white `COLOR`, was
retained.

- `d3d12.dll`: `FB3DB807D4F8897594E9C130E917D677CD12DA6150E36AD4C46BCCBA4EAF29D8`.
- `dxgi.dll`: `59D5A4111E91F55093E8246A8854B19810EEBEF72A3F065F1135FDDA16597B10`.
- VS `20e0826b236597a1.cso`: `0DD0F85D48D1E27852B99A696017A5DBBE84AAA4562271EDCF139D94AB8B39F8`.
- PS `44b0a992c2304254.cso` and `d77600bde0076c33.cso`: both
  `BABC55F09FF9BD5C3F5C4C1934662CF2FF5581EFDF48C934A91B22A7C08B8A92`.

## Result

The target t0 readback was white (`RGBA=(255,255,255,255)`). Both target
shaders executed on the resource captured as the Present #100 source:
`ps_invocations=921600` for PS `44b0a992c2304254` and `230400` for PS
`d77600bde0076c33`. Nevertheless, both 1280x720 source and backbuffer BMPs
were completely black: **0/921,600 non-black pixels**. Their identical
SHA-256 is
`D43EC90910E666B315661BE75DFEA0AF9722FF471AA30593999567DFFC53AC9C`.

This rules out a skipped target draw and shows that bypassing the later
gamma/constant-buffer operations is insufficient. It does not establish
which value reaches the PS at each pixel: the t0 probe samples the bound
resource, while the actual PS sample coordinates and interpolated `COLOR`
have not yet been observed in shader output. The next discriminating test
should write sampled texel, interpolated `COLOR`, and their product to
separate output channels.

## Evidence and cleanup

- `d3d12on11-v009.log` - raw log, SHA-256
  `4D083C9EB0F15CDDFBB9B3DA7905C279FF5A7CC889DC791957AECA2147E292C8`.
- `run-manifest.json` - SHA-256
  `2929DF786960A42BA47C2C2D3A8DC55D9A2BD20DBA68E3ECBF6DE1A04C182426`.
- `HelaReadback_Present100_source.bmp` and
  `HelaReadback_Present100_backbuffer.bmp` - full-frame readbacks.
- `overrides/` - exact override bytecodes used in the run.
- After the run, the game-root DLLs were restored to v001 and the normal
  v005 override set was restored. The restored DLL hashes were verified.
