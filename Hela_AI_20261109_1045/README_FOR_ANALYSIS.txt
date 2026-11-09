HELA D3D12-TO-D3D11 PROXY - ANALYSIS HANDOFF
Run date: 2026-11-09

PURPOSE
Investigate why the Hela D3D12-to-D3D11 proxy still produces no verified
graphics. This focused package contains the targeted shader conversions,
pixel-shader tracing, and a tested staging path for compressed mip tails.
It is for code and log review, not a complete copy of the game or assets.

PROJECTS AND REPRODUCTION
- `Plugin Nuovi\sm6-to-sm5-mcp` is the Shader Lab: its TypeScript MCP server
  analyzes/converts shader HLSL and prepares SM5 candidates.
- `Plugin Nuovi\sm6-to-sm5-mcp\d3d12on11` is a separate native C++ project
  colocated in that workspace. It builds the D3D12-to-D3D11 proxy DLLs and
  contains the proxy's CMake build and native smoke tests. Hela in-game
  proxy runs use these DLLs; they are not runs of the Shader Lab server.
- `proxy-source` in this package is the portable Git snapshot of the
  `d3d12on11` CMakeLists, source, generated headers, tests, and tools.
  The local workspace itself is not a Git checkout. Use this committed
  snapshot to reproduce the proxy build on another PC; do not copy the
  machine-specific `build*` or `backups` directories.
- `runtime\bin` contains the exact diagnostic DLL artifacts used by the
  documented Hela test. The CMake source snapshot is the build input;
  `evidence\build-provenance.txt` records the tested build and DLL hashes.
- `runtime\crash-diagnostics` contains a separate diagnostic build with
  optional first-chance access-violation reporting and its matching PDB. It
  passed native tests but has not yet been run in Hela; it does not replace
  the exact tested binaries in `runtime\bin`.

LATEST FOLLOW-UP - 2026-11-09
- Signature investigation identified the first rejected candidate as VS
  4ce363172cfd70be: its original DXIL inputs are ATTRIBUTE0/1, while the
  initial SM5 candidate declared TEXCOORD0/1. Both corrected VS candidates
  now match their original input signatures; the next run recorded zero
  input-layout failures. See evidence\input-signature-analysis-20261109.txt.
- Fixed the converter entry-point selection so HLSL with a SPIRV-Cross
  wrapper is compiled through `main`, not its internal `vert_main` helper.
  The helper-only bytecode had empty input/output signatures.
- Recompiled VS 20e0826b236597a1 and 4ce363172cfd70be through `main`.
  Their input signatures match the original DXIL signatures. Both shaders
  loaded, with zero INPUT_LAYOUT_FAILED and zero PSO GRAPHICS FAILED.
- Final heartbeat: present=1817, draw(ok)=2667, rtv=2667, bb=0,
  skip-vs=0, STAGING_CREATE_FAIL=0, placed overlap=0.
- The user observed a game window and audio but no visible game graphics.
  No backbuffer-targeted draw was recorded, so this is not a successful
  visual-rendering result. The process exited with 0xC0000005 after about
  44 seconds. No matching UE crash context or Windows application-error
  event was found; the cause of the access violation is unknown.
- The proxy's V7 d3d12.dll and dxgi.dll and the conservative INI were
  restored after the test. Only the original shader overrides remain
  installed in the game.
- Full trace and measured results: evidence\vertex-wrapper-fix-test-20261109.log
  and evidence\vertex-wrapper-fix-test-20261109.txt.
- Added an optional first-chance access-violation handler to the proxy.
  Release CTest passed 6/6, including a synthetic exception test that
  verified the report, captured stack, and minidump. This is a local native
  test only; the new build has not yet been run in Hela. See
  evidence\crash-handler-build-test-20261109.txt.

TEST SETUP
- The game ran on this PC's GTX 1050 Ti. The destination GTX 950M has not
  been tested.
- The prior diagnostic run used the proxy DLLs in runtime\bin, diagnostic
  INI in runtime\config, and two original SM5 overrides. A later controlled
  run loaded all five additional SM5 candidates, but Hela asserted during
  graphics PSO creation; no successful draw was verified.
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
- proxy-source: portable CMake project snapshot, including proxy source,
  generated headers, native tests, and build/helper tools.
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
- evidence\shader-candidate-test-20261109.log: complete diagnostic log from
  the controlled run with all five candidate overrides installed.
- evidence\shader-candidate-crash-20261109.xml: UE crash context for that
  run's PipelineStateCache assertion.
- evidence\shader-candidate-test-20261109.txt: test setup, measured results,
  and limits of the conclusions.
- evidence\vertex-wrapper-fix-test-20261109.log and
  evidence\vertex-wrapper-fix-test-20261109.txt: full trace and results
  from the corrected VS wrapper-entry-point test.
- evidence\input-signature-analysis-20261109.txt: signature extraction and
  comparison for the first rejected VS and both corrected candidates.
- evidence\input-signature-analysis-20261109.txt: original DXIL and
  pre-/post-fix SM5 input signature comparison, including the first failed
  VS and the association limit for the remaining six-element failures.
- proxy-source\src\crash_handler.cpp and
  proxy-source\tests\crash_handler_smoke.cpp: optional first-chance AV
  report/minidump diagnostic, disabled by default and tested with CTest.
- runtime\crash-diagnostics\d3d12.dll and d3d12.pdb: fresh Release
  diagnostic build and matching symbols for the next controlled test.
  Keep the PDB available to WinDbg; this build is not the binary used for
  the documented Hela run.
- evidence\crash-handler-build-test-20261109.txt: build hashes, native
  smoke-test result, and limitations of the new handler.
- evidence\converter-source: snapshots of the converter post-processor and
  regression tests used for the corrected candidates. The editable source
  remains in the local sm6-to-sm5-mcp project.
- evidence\progress-history.txt: chronological project/test record,
  updated with the candidate and corrected-entry-point runs.
- evidence\build-provenance.txt and evidence\SHA256SUMS.txt: build details
  and hashes for package files.

The older verbose raw logs (including the 19.1 MB instrumented run) are
intentionally left out to avoid noise. The complete candidate-test log and
the later 14.7 MB corrected-entry-point trace are included because they
record the shader loads and failures needed for diagnosis.
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
