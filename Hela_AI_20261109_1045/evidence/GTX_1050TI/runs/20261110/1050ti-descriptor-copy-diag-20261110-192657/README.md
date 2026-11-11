# 1050 Ti — diagnosi CopyDescriptors

Alle 19:27 il trace registra una chiamata con
`pSrcDescriptorRangeSizes == nullptr`, un solo range sorgente e un solo
range destinazione (`type=0`). Al draw bersaglio t0 non risultava ancora
bindata (`srv_mask=0`).

Estratti: [`../diagnostic-extracts.txt`](../diagnostic-extracts.txt).
Log raw locale:
`D3D12On11Patch/versions/1050ti-descriptor-copy-diag-20261110-192657/run/d3d12on11.log`.
