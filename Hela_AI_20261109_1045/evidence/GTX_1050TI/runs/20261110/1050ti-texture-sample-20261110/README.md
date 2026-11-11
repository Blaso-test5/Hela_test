# 1050 Ti — PS texture sample

Il test PS campiona t0. Readback sorgente e backbuffer: 1360x768, tutti i
1.044.480 pixel sono `RGB(0,0,0)`. La preview lossless e'
[`readback.png`](readback.png); l'hash del BMP coincide con il readback del
PS reale convertito.

Il contenuto del texel di t0 non e' stato acquisito in questo run.

Log raw locale:
`D3D12On11Patch/versions/1050ti-texture-sample-20261110/run/d3d12on11.log`.
