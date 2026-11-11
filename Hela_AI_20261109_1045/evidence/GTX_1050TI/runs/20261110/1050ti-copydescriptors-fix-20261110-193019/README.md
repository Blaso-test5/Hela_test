# 1050 Ti — CopyDescriptors corretto

Dopo aver trattato gli array di range size nulli come una copia per range,
il draw ha SRV t0 e sampler s0 bindati (`srv_mask=0x0001`). t0 e' una
texture 1x1; la SRV usa formato 28 (`R8G8B8A8_UNORM`) e la risorsa formato
27 (`R8G8B8A8_TYPELESS`). Il readback e' composto da 1.040.400 pixel neri
e 4.080 bianchi; il PS reale del run successivo produce nero uniforme.
La preview lossless e' [`readback.png`](readback.png).

Estratti: [`../diagnostic-extracts.txt`](../diagnostic-extracts.txt).
Log raw locale:
`D3D12On11Patch/versions/1050ti-copydescriptors-fix-20261110-193019/run/d3d12on11.log`.
