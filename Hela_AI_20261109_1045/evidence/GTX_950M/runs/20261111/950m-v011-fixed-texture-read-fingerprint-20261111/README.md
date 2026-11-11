# GTX 950M v011 — fixed-coordinate texture reads

Run date: 2026-11-11.

The manifest-gated run used the v008 proxy pair and fullscreen VS. The two
target PS overrides compared `Texture2D.Load(int3(0,0,0))`
(`44b0a992c2304254`) with `SampleLevel(float2(0.5,0.5), 0)`
(`d77600bde0076c33`). The launcher verified the installed six-file override
set before launch and confirmed the loaded module and required-shader
fingerprints afterward.

- Both target PSs executed on the Present #100 source: 921,600 invocations
  for `44b0a992c2304254` and 230,400 for `d77600bde0076c33`.
- The full 1280x720 source and backbuffer captures at Present #100 are both
  uniformly black: 0/921,600 non-black pixels. Both BMPs have SHA-256
  `D43EC90910E666B315661BE75DFEA0AF9722FF471AA30593999567DFFC53AC9C`.
- At Present #1300, all five source and backbuffer sample points are white
  (`RGBA=(255,255,255,255)`). This is a five-point readback, not a full-frame
  capture. It demonstrates the probe output changed over time, but does not
  independently establish the result of both sampling operations.
- Raw runtime log SHA-256:
  `C9D6653FB7D75E6BF6ADBB2888070660DA12A7B0E762BDA0F4517D1A7A296B11`.
- Run manifest SHA-256:
  `CBF3CF19C4E4978F4DCD0288EA1909C33A336AF50CD04AE388DF234D6CA9232E`.
- Runtime module hashes: `d3d12.dll`
  `FB3DB807D4F8897594E9C130E917D677CD12DA6150E36AD4C46BCCBA4EAF29D8`;
  `dxgi.dll`
  `59D5A4111E91F55093E8246A8854B19810EEBEF72A3F065F1135FDDA16597B10`.
- The two target CSO hashes are recorded in the manifest and attested in the
  raw log. Both installed game DLLs were restored to v001 after the run.

The proxy writes its Present #100 BMPs to `D:\HelaReadback_Present100_*.bmp`;
they were copied here after verifying the logged paths and pixel data. The
monitor's requested BMP paths do not change the proxy's hard-coded capture
paths. The proxy also only writes a full BMP at Present #100, so the later
white sample points cannot be treated as proof of a white full frame.

This is diagnostic output, not visible Hela gameplay. The next comparison
must isolate the two read operations after the texture becomes readable and
capture a full frame at that later point.
