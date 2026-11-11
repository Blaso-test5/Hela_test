# 1050 Ti — readback IB

Ora: 18:58. Il draw target richiede sei indici. La shadow CPU contiene
`[0,1,2,2,1,3]`, mentre il readback del buffer D3D11 contiene
`[0,0,0,0,0,0]`. Il flush dell'upload prima del bind IA non era ancora
presente.

L'estratto misurato e' in
[`../diagnostic-extracts.txt`](../diagnostic-extracts.txt). Log raw locale:
`D3D12On11Patch/versions/1050ti-index-data-20261110-185841/run/d3d12on11.log`.
