# 1050 Ti — flush upload IB/VB

Ora: 19:00. Dopo il flush dell'upload prima del bind IA, il readback D3D11
degli indici coincide con la shadow CPU:
`[0,1,2,2,1,3]`.

L'estratto misurato e' in
[`../diagnostic-extracts.txt`](../diagnostic-extracts.txt). Log raw locale:
`D3D12On11Patch/versions/1050ti-upload-flush-20261110-190026/run/d3d12on11.log`.
