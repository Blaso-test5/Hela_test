HELA D3D12-TO-D3D11 PROXY - ANALYSIS HANDOFF
Run date: 2026-11-09

PURPOSE
Investigate why the Hela D3D12-to-D3D11 proxy still produces no verified
graphics. This focused package contains the targeted shader conversions,
pixel-shader tracing, and a tested staging path for compressed mip tails.
It is for code and log review, not a complete copy of the game or assets.

TEST SETUP
- The game ran on this PC's GTX 1050 Ti. The destination GTX 950M has not
  been tested.
- The prior diagnostic run used the proxy DLLs in runtime\bin, diagnostic
  INI in runtime\config, and two original SM5 overrides. Five additional
  SM5 candidates are included but were not successfully exercised in Hela.
- diagnostic_render_trace, caller tracing, and diagnostic capabilities
  were enabled for this run only. The diagnostic-capabilities option is
  explicitly marked unsafe by the proxy; do not treat this INI as a
  production configuration.
- The game's native D3D11 renderer is not enabled. Hela still calls D3D12;
  the proxy translates only the D3D12 subset it implements to D3D11.
- This PC's GTX 1050 Ti was used. The destination GTX 950M remains untested.

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
4. The follow-up trace identifies missing VS hashes
   20e0826b236597a1 and 4ce363172cfd70be. Their initial PS pairs are
   d77600bde0076c33 and 4c1bc8bf7e457acd, respectively. PS 44b0a992c2304254
   (3,744 bytes) appeared immediately before skips on both pipeline
   instances using VS 20e, but the prior trace did not record bound PS
   hashes. Treat it as a candidate, not a proven per-pipeline association.
5. Do not equate PSO creation or successful Present with correct rendering.
   Do not infer GTX 950M compatibility from this GTX 1050 Ti test.
6. Five candidate shader pairs/inputs were converted to HLSL and compiled
   with FXC to SM5: VS 20e0826b236597a1; PS d77600bde0076c33 and
   44b0a992c2304254; VS 4ce363172cfd70be; PS 4c1bc8bf7e457acd.
   FXC compilation is not proof of semantic equivalence or runtime use.
7. Added PS hash to skipped-draw trace. A Steam-launched attempt with the
   diagnostic DLL initialized and destroyed D3D12 devices but exited before
   PSO creation or any draw. No candidate shader was confirmed loaded and
   no rendering conclusion can be drawn from that attempt.
8. The BC staging fix creates an aligned staging mip chain for BC
   sub-block footprints, maps the mip whose dimensions match the footprint,
   and uses that same subresource for copy/readback. The D3D11 smoke test
   verified BC1 and BC7 upload/readback through a 4x4 mip chain's 2x2 mip;
   standalone 2x2 staging still returns E_INVALIDARG as expected.
   In-game behavior of this fix remains unverified.

CONTENTS
- proxy-source: CMakeLists.txt plus the proxy src, generated headers, and
  smoke-test sources needed to inspect the implementation.
- runtime\bin: exact diagnostic d3d12.dll and dxgi.dll used in the run.
- runtime\config: diagnostic INI and binding map used in the run.
- shaders\original-dxil: dumped shader inputs, including the additional
  candidate PS 44b0a992c2304254.
- shaders\hlsl: original HLSL plus converted HLSL candidates.
- shaders\overrides: original and newly compiled SM5 DXBC candidates. The
  additional candidates are not installed in the persistent game setup.
- evidence\render-trace-extract.txt: concise, relevant records and counts.
- evidence\instrumented-followup-test.txt: follow-up run identifying the
  missing VS hashes and reproducing the BC staging-size failure directly
  against D3D11.
- evidence\bc-mip-staging-test.txt: BC1/BC7 mip-tail upload/readback results.
- evidence\steam-candidate-shader-run.log: short Steam-started attempt that
  exited before pipeline creation; included to document the limitation.
- evidence\progress-history.txt: chronological project/test record,
  updated with this run.
- evidence\build-provenance.txt and evidence\SHA256SUMS.txt: build details
  and hashes for package files.

The verbose raw logs (including the 19.1 MB instrumented run) are intentionally
left out to avoid noise.
The complete original log remains on the test PC under:
D:\SteamLibrary\steamapps\common\Hela Demo\Hela\Binaries\Win64\D3D12On11Patch\backups\pre-render-trace-override-test-20261109-1045\d3d12on11-render-trace.log

A second, instrumented follow-up log is also retained locally under:
D:\SteamLibrary\steamapps\common\Hela Demo\Hela\Binaries\Win64\D3D12On11Patch\backups\pre-instrumentation-test-20261109\d3d12on11-instrumented-run.log

The exact diagnostic d3d12.dll in runtime\bin has SHA-256
13646DD1A826E1DF864B175AD0E4205EEF31DEAEDF8E01772E4B99F8085FCA7B.
The Hela installation was restored to V7 d3d12.dll
(SHA-256 8C92E594CBF963DE94FE2957FE6162A0D1F3E1CD718B545D3B1808AAF1FF9CC2),
the prior dxgi.dll, and conservative INI. Only the original two overrides
remain installed. No game executable or assets are included.
