HELA D3D12-TO-D3D11 PROXY - ANALYSIS HANDOFF
Run date: 2026-11-09

PURPOSE
Investigate why the Hela D3D12-to-D3D11 proxy still produces no verified
graphics, despite compiling and loading matching SM5 overrides for one
vertex/pixel shader pair. This is a focused package for code and log review,
not a complete copy of the game or its assets.

TEST SETUP
- The game ran on this PC's GTX 1050 Ti. The destination GTX 950M has not
  been tested.
- The diagnostic run used the D3D12On11 proxy DLLs in runtime\bin, the
  diagnostic INI in runtime\config, and the two SM5 overrides in
  shaders\overrides.
- diagnostic_render_trace, caller tracing, and diagnostic capabilities
  were enabled for this run only. The diagnostic-capabilities option is
  explicitly marked unsafe by the proxy; do not treat this INI as a
  production configuration.
- The game's native D3D11 renderer is not enabled. Hela still calls D3D12;
  the proxy translates only the D3D12 subset it implements to D3D11.

TEST RESULT
- The proxy loaded VS a7f30e5d1d5fc6fb (692-byte vs_5_0 override) and PS
  f79b80f0f2f58f68 (796-byte ps_5_0 override).
- Graphics PSO #1 was created with status=ready. This proves that the
  proxy accepted both shader overrides and created a D3D11 graphics PSO;
  it does not prove that this PSO was used by a draw.
- The run logged 1,290 successful swap-chain Presents and no failed
  Present calls. Present success alone does not prove that useful pixels
  were rendered.
- The final render-trace heartbeat reported draw(ok)=0 and skip-vs=2001.
  In commands.cpp, skip-vs counts draw commands skipped because the active
  graphics pipeline has a missing vertex shader. The trace does not record
  the PSO hash for each skipped draw, so it cannot identify which shader
  override is needed next.
- The log contains 89 graphics PSO placeholder results and 104 compute PSO
  placeholder results, in addition to the ready graphics PSO #1.
- 1,087 attempts to create a texture staging resource for upload failed
  with HRESULT 0x80070057 (E_INVALIDARG). This is a separate, concrete
  issue to investigate; its causal relationship to the missing image has
  not been established.
- This run did not reproduce the earlier GetClockCalibration
  0x887a0005 error. No correct visible frame was verified.

INTERPRETATION / REVIEW QUESTIONS
1. Trace the exact PSO bound at every draw and identify which missing VS
   causes the skip-vs counter to increase. Then select a relevant shader
   for conversion rather than converting shaders at random.
2. Investigate the texture-upload path around CreateStagingTexture and the
   repeated E_INVALIDARG results. Log the resource dimension, format,
   footprint width/height/depth, and D3D11 texture descriptor before
   changing behavior.
3. Check whether the ready PSO #1 is ever selected for a draw, whether its
   root-signature/resource bindings are valid, and whether a successful
   draw reaches the back buffer before Present.
4. Do not equate PSO creation or successful Present with correct rendering.
   Do not infer GTX 950M compatibility from this GTX 1050 Ti test.

CONTENTS
- proxy-source: CMakeLists.txt plus the proxy src, generated headers, and
  smoke-test sources needed to inspect the implementation.
- runtime\bin: exact diagnostic d3d12.dll and dxgi.dll used in the run.
- runtime\config: diagnostic INI and binding map used in the run.
- shaders\original-dxil: original dumped DXIL inputs for the tested VS and
  PS.
- shaders\hlsl: converted HLSL candidates.
- shaders\overrides: compiled SM5 DXBC files.
- evidence\render-trace-extract.txt: concise, relevant records and counts.
- evidence\progress-history.txt: chronological project/test record,
  updated with this run.
- evidence\build-provenance.txt and evidence\SHA256SUMS.txt: build details
  and hashes for package files.

The verbose 18.6 MB raw run log was intentionally left out to avoid noise.
The complete original log remains on the test PC under:
D:\SteamLibrary\steamapps\common\Hela Demo\Hela\Binaries\Win64\D3D12On11Patch\backups\pre-render-trace-override-test-20261109-1045\d3d12on11-render-trace.log

The tested diagnostic DLLs are not the DLLs left installed after the run.
The Hela installation was restored to its prior V7 DLL and conservative INI;
the shader overrides remain installed. No game executable or game assets
are included in this package.
