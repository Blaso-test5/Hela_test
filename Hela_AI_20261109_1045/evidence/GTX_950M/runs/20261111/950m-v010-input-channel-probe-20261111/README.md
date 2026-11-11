# GTX 950M v010 - sampled texture and COLOR channel probe

Run date: 2026-11-11.

The manifest-gated run used the v008 proxy pair and v007 fullscreen VS, with
target PS outputs set to `(sampled t0.r, COLOR.r, sampled t0.r * COLOR.r)`.
The log attests the exact modules and all required target overrides.

- t0 staging readback: `RGBA=(255,255,255,255)`.
- Both target PSs executed against the Present #100 source resource:
  `44b0a992c2304254`: 921,600 PS invocations;
  `d77600bde0076c33`: 230,400 PS invocations.
- Both Present #100 source and backbuffer BMPs are 1280x720, 24-bit, and
  uniformly green: `(R,G,B)=(0,255,0)` for all 921,600 pixels. Their SHA-256
  is `94038898ED4F9F4C358A02C68F4AA329F090645AD81F000C551B8359DC2459F7`.
- Raw log SHA-256:
  `5C55FA4C4D8E25598B9B89D2C75F674620A407255CCED8EFDE7CF67DB03BF8BD`.
- Run manifest SHA-256:
  `1A035AE810176BDCEC521AC8658E6E03DECC3F939E7EACF9AFA16F46DA4585A2`.

The green result means the tested VS COLOR red channel reached the PS as 1,
while the PS's ordinary sampled texture red channel was 0. Its product was
also 0. The white staging readback only establishes that the bound 1x1 SRV
contains white; it does not prove that the PS sampling operation returns that
texel.

This rules out failed VS COLOR propagation and confirms that the target draw
and backbuffer copy work. It narrows the next investigation to texture
sampling, SRV interpretation, or the shader's resource/coordinate path. v011
compares direct `Texture2D.Load(int3(0,0,0))` with `SampleLevel` at fixed
center coordinates. Both root DLLs were restored to v001 after this run.
