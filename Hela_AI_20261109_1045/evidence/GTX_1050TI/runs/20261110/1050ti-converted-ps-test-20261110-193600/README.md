# 1050 Ti — PS reale convertito

Il VS `20e0826b236597a1` (1768 byte) e il PS
`44b0a992c2304254` (1560 byte, `ps_5_0`) sono caricati. Al draw target
gli indici D3D11 sono `[0,1,2,2,1,3]`; le statistiche contano 4 invocazioni
VS, 2 primitive clip e 1.044.480 invocazioni PS. Il readback sorgente e
backbuffer e' nero uniforme (`1360x768`).

La preview [`../1050ti-texture-sample-20261110/readback.png`](../1050ti-texture-sample-20261110/readback.png)
ha lo stesso hash del BMP sorgente e backbuffer di questo run. Il log raw
e' [`d3d12on11.log`](d3d12on11.log). Estratti:
[`../diagnostic-extracts.txt`](../diagnostic-extracts.txt).
