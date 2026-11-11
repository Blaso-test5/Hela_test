# GTX 950M v013 — encoded `SampleLevel` probe

Run date: 2026-11-11.

This run used the same v012 proxy pair, with distinct `SampleLevel` pixel
shader bytecodes for the two target shader hashes. The manifest gate and
post-run module/shader fingerprint check passed.

- The t0 staging readback at the first target hit was white
  (`RGBA=(255,255,255,255)`).
- Both target PS overrides executed on the Present #100 source resource.
  The full 1280x720 source and backbuffer BMPs are uniformly `(64,64,159)`
  (921,600 pixels); both have SHA-256
  `AB36B3D6BA0F65A189D03BEB79F3D2596AF6614E63CE0199EE8C964732D6B483`.
  The encoded red/green therefore indicate zero sampled red/green at this
  initial capture. The single blue value does not visibly distinguish the
  two PS variants in the final frame.
- The runtime log's last periodic source readback is Present #800. There is
  no `READBACK_FULL` record for Present #1300, so no late-frame conclusion is
  supported.
- The run directory initially received two Present #1300 BMPs left over from
  v012: their timestamps predate this run and their hash matches v012. Those
  stale files have been removed; only the Present #100 captures are retained.
- The log also records `CreateShaderResourceView` failure
  `hr=0x8007000e`. The same failure appears in v012 and v014; its relationship
  to the incomplete later readbacks has not been established.

This proves only the initial encoded `SampleLevel` output, not whether the
probe changes at later Presents or whether Hela's real rendering works.
Repeat the late capture with freshness checks before comparing it with v012.

- Raw log SHA-256:
  `B9E8EAD902B42329C32145848C4494E70F990A9692C785C597473C810F632245`
- Run manifest SHA-256:
  `4A48A5CDDA11BA5F870350CAD15AF0734F836A83D246F27EC992AE3688950555`
- Override SHA-256: `44b0a992c2304254.cso`
  `C8120FE394213FD65BC2967459807F0C3F851DE2AE2B69690B31166CFF5AC7E1`;
  `d77600bde0076c33.cso`
  `576627387F3BCD1676ABB819C76C061CC8E315029BAE0E12BFD1B3CDDF47DD51`.
- Runtime module hashes: `d3d12.dll`
  `58AF854FC49EEC4D78F011DF7E7A39AF568631BF1B687337CD20460FA91DCE84`;
  `dxgi.dll`
  `81EC9BD66291840983D3FBAF24CCF9E1A5F7884D6A334F6C8DB59D20135AC6A4`.

The installed game DLLs were restored to v001 and the override set to v005.
