# GTX 950M v012 — encoded `Load` probe

Run date: 2026-11-11.

This manifest-gated run used the v012 proxy pair and fullscreen vertex shader.
Both target pixel-shader overrides used the same `Texture2D.Load` bytecode
(SHA-256 `CF5284CA6ED6A8A2C66699DCC45B95BF9DCA4D081CE1EF811F104316F6C2383D`),
so this run did not compare the two target draws' texture-read operations.
The probe encoded sampled red/green into output red/green and used blue to
indicate valid texture dimensions.

- At Present #100, the full source and backbuffer BMPs are uniformly
  `(64,64,191)` across 921,600 pixels. Their SHA-256 is
  `036EF0698A6652FEDE554ECA39F64B2CAAD2DB616298EF42E48B5D70732BB66D`.
  This encodes zero sampled red/green and valid dimensions.
- At Present #1300, both full BMPs are uniformly `(191,191,191)` across
  921,600 pixels. Their SHA-256 is
  `5A84CBCDBBB4D320D549439E60E3614F9A29BE9225238F814679B61067BF5C19`.
  This is consistent with the sampled red/green channels becoming one.
- Periodic readbacks changed from the initial encoded value between Presents
  #300 and #500. The t0 staging readback at the first target hit was white
  (`RGBA=(255,255,255,255)`), so staging and shader-observed values did not
  agree at the initial capture.
- The target shaders executed on the Present #100 RTV; the exact bindings,
  readbacks, and invocation counts are in the raw log.

The output is shader-generated diagnostic color, not Hela gameplay. The
identical bytecode also means v012 cannot establish whether `Load` and
`SampleLevel` behave differently.

- Raw log SHA-256:
  `1BD32BB0FEB541844A6F8B9890E889734BA266DA773E15E6B02B63135DA42FFD`
- Run manifest SHA-256:
  `B19DF95044088EE1411309FB5E860DE8231ADDB0C06E9D9A5184B69299692D80`
- Runtime module hashes: `d3d12.dll`
  `58AF854FC49EEC4D78F011DF7E7A39AF568631BF1B687337CD20460FA91DCE84`;
  `dxgi.dll`
  `81EC9BD66291840983D3FBAF24CCF9E1A5F7884D6A334F6C8DB59D20135AC6A4`.

The launcher verified the run manifest and runtime/shader fingerprints.
Afterward, the installed game DLLs were restored to v001 and the override set
to v005.
