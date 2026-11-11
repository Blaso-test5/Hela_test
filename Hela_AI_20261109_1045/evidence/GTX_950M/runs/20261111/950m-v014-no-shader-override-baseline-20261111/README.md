# GTX 950M v014 — no-shader-override baseline

Run date: 2026-11-11.

This control run used the v012 proxy pair with an empty shader-override set.
The launcher accepted the empty manifest, verified both runtime module
fingerprints, and completed its post-run attestation.

- The full 1280x720 source and backbuffer captures at Present #100 are both
  uniformly black: 0/921,600 non-black pixels. Both BMPs have SHA-256
  `D43EC90910E666B315661BE75DFEA0AF9722FF471AA30593999567DFFC53AC9C`.
- At Present #400, all five source and backbuffer sample points are black.
  The matched RTV history reports `draws=0, skipped=66, clears=0`.
- Present #400 is the last readback in the log. No later full-frame capture
  was made, so this run does not characterize later Presents.
- The raw log records a `CreateShaderResourceView` failure with
  `hr=0x8007000e`; this also appears in v012 and v013 and is not yet tied
  causally to the black output.

The baseline is strong evidence that the actual scene is not rendering
through this no-override configuration on the GTX 950M. The colored v012/v013
frames are shader-generated probes, not evidence of working gameplay.

- Raw log SHA-256:
  `34219ED13D0A8699A7E8028CE0C915E265984200C0B3F056456874B76246CD5E`
- Run manifest SHA-256:
  `1EBBE58E59A89C613BC015FFB9F0E6B8E0EDED2678C692D1BE6A683DE7F5ED58`
- Runtime module hashes: `d3d12.dll`
  `58AF854FC49EEC4D78F011DF7E7A39AF568631BF1B687337CD20460FA91DCE84`;
  `dxgi.dll`
  `81EC9BD66291840983D3FBAF24CCF9E1A5F7884D6A334F6C8DB59D20135AC6A4`.
- `overrides` and `requiredOverrides` were empty in the manifest. The six
  installed v005 overrides were restored after the run; installed DLLs were
  restored to v001.
