# GTX 950M v008 — solid-white PS and runtime fingerprints

Run date: 2026-11-11.

## Reproducible identity gate

The launcher verified `run-manifest.json` before starting Hela and verified
the runtime log attestations after exit. The manifest pins both installed
DLLs and all six `.cso` files; the three target shaders are required in the
log.

- `d3d12.dll`: SHA-256
  `FB3DB807D4F8897594E9C130E917D677CD12DA6150E36AD4C46BCCBA4EAF29D8`.
- `dxgi.dll`: SHA-256
  `59D5A4111E91F55093E8246A8854B19810EEBEF72A3F065F1135FDDA16597B10`.
- The log attests these same module hashes and the exact loaded target
  overrides: VS `20e0826b236597a1.cso`
  `0DD0F85D48D1E27852B99A696017A5DBBE84AAA4562271EDCF139D94AB8B39F8`;
  PS `44b0a992c2304254.cso` and `d77600bde0076c33.cso`, both
  `25C04CF8F525E6273BDFC73F6CEF93ACEE6DDED385882E4A99B303852B00F9EB`.
- The PS bytecode was compiled as `ps_5_0` from the two
  `*.white-output.hlsl` files in this directory. The test VS is the
  v007 white-color fullscreen VS.

## Verified result

Both target PS shaders were changed only at their final output assignment to
write `RGBA=(1,1,1,1)`. Present #100 source and backbuffer BMPs are 1280x720,
24-bit, and contain **921,600/921,600 non-black pixels**. Both files have
SHA-256
`A177D96DA845E94F6B13C6B1F4EDC42BC880DE18C2FE16A1916AA3651E1C2DAB`.

The forced PS result proves the test draw can rasterize into the copied source
and reach the backbuffer. In v007, the same test VS positions with white
`COLOR`, but the converted game PSs, still yielded black. The remaining
failure is therefore in the converted PS behavior or its inputs/translation,
not an inability of this target path to write pixels. This is a forced
diagnostic frame, not Hela's normal game image.

## Evidence and cleanup

- `d3d12on11-v008.log` — raw log, SHA-256
  `C3F79011D72791AB9996054CCA3BAD3DB3F631A7B2A9C9821EBDCB9386C42A12`.
- `run-manifest.json` — expected DLL and complete override-set hashes.
- `HelaReadback_Present100_source.bmp` and
  `HelaReadback_Present100_backbuffer.bmp` — full-frame readbacks.
- `overrides/` — exact installed override set; the two modified PS bytecodes
  have identical hashes because both compile to the same constant-output
  shader.
- The game-root DLLs were restored to v001 and the original v005 override set
  was restored after the run. The temporary v008 DLL/override pair remains
  archived under the machine's `D3D12On11Patch\versions` directory.
