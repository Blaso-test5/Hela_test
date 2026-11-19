# Compute candidate dispatch smoke — 2026-11-19

## Result

The Hela compute shader candidate `bb592abf5ce2bad1` was exercised through
the current D3D12-to-D3D11 proxy using its original DXIL and the matching
SM5 override:

- The proxy reported `SHADER READY` with `source=SM5-override`,
  `PSO COMPUTE ... status=ready`, `missing_mask=0x00`, and `d11_cs=1`.
- The isolated test issued one compute dispatch using the candidate's actual
  `b0`/`t0`/`t1`/`u0` interface and deterministic input buffers.
- Readback matched all 64 output records, each containing four expected
  32-bit values. The test waited for the proxy fence before validating data.
- All five committed resources, both SRVs, and the UAV were created
  successfully.

The exact inputs were:

- Original DXIL, 2,532 bytes, SHA-256
  `4EB6AAD589ACCDA4E0814226C4DF73182DE0B5EA000CAF9224B3FC1C9B12F97D`.
- SM5 override, 1,288 bytes, SHA-256
  `98AA22EA7D622C246FD30131C4743466FFD07328B9BD457D3B14F9F0AD97C6C6`.
- Proxy DLL SHA-256
  `5BA2784B02B1FF698EC1C9A199B65CCAA03E6DA6A2C73DD1185C479A5A25AEAE`.

The test ran on the current host, which Windows identifies as a GTX 1050 Ti
with driver `32.0.15.8278`. The proxy log reports the default hardware
adapter, D3D11 feature level `11_1`, and tiled-resources tier 2.

## What this establishes

This is stronger than PSO-creation-only validation: the candidate override
can execute a dispatch and produce the expected buffer output through the
proxy on this host. The candidate hash has appeared as a missing-CS
placeholder in historical GTX 950M diagnostic output, making it a useful
candidate for a later targeted 950M test.

This does **not** establish that Hela dispatches this shader during startup,
that the shader is semantically equivalent to the original DXIL, or that it
works on the GTX 950M. The 950M reports D3D11 tiled-resources tier 1, while
this host reports tier 2. Hela was not launched; the DXIL and CSO were used
only in an isolated local test. Shader contents and test binaries are not
included in this report.
