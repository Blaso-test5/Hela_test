# Hela GTX 950M checkpoint — 2026-11-11

## Goal and status

Continue the controlled investigation until Hela renders a visible game image
on the GTX 950M. The goal is **not yet reached**. v012's `Load` probe changed
from an encoded zero at Present #100 to encoded ones by #1300, but that is
shader-generated output, not gameplay. v013's `SampleLevel` probe also
produced encoded zero at #100; its log ends at #800 and has no verified late
capture. v014's no-override baseline is uniformly black at #100 and reports
66 skipped draws at #400. The forced-color path and instrumentation work, but
real Hela rendering has not been demonstrated.

The versioned proxy source now contains bounded target-draw diagnostics,
pipeline statistics, t0 texel readback, input-layout logging, and a four-vertex
readback. Each run restored the game directory to v001. Hela is closed. The
previous checkpoint remains at
[`checkpoint-20261110.md`](./checkpoint-20261110.md).

## Diagnostic patch

The helper in `../../proxy-source/src/commands.cpp` records at most six
matching draws overall and at most two per RTV resource. It logs the actual
RTV/resource against the most recent CopyResource source, draw arguments,
VB/IB bindings and index data, CBV/SRV/sampler bindings, viewport/scissor,
rasterizer, blend, and depth/stencil state. Bounded pipeline-statistics
queries report whether vertex and pixel shader invocations occurred.

The diagnostics also copy bounded portions of t0, the index buffer, and the
first four target vertices to staging resources for readback, and issue
pipeline-statistics queries. They do not clear or write the render target, but
may perturb timing. Null depth state is reported with D3D11's effective
default depth enable/function/write mask; blend factors are logged explicitly.
New runtime diagnostics now log SHA-256 fingerprints for the loaded d3d12 and
dxgi proxy modules and every SM5 shader override. The live-run launcher also
requires a per-run manifest and aborts on a preflight mismatch or missing
post-run fingerprint.

Argon's focused review highlighted the default-state ambiguity that was
corrected. The merged source builds with Visual Studio 18 / MSVC
19.50.35721.0 using the `d3d12` and `dxgi_proxy` targets. Five of six CTest
tests pass; `d3d12on11_smoke` fails on the existing tiled-resource layout
expectation (`tiles=1, packed-mips=0, subresources=1`). The other five tests
pass when that test is excluded. The temporary DLLs remain outside the
repository and the installed root pair is restored to v001.

## GTX 1050 Ti evidence from the merged update

- The target VS and PS ran: the converted-PS test recorded 1,044,480 PS
  invocations, but the actual-shader source/backbuffer readbacks remained
  black.
- A magenta PS override produced a full magenta readback; therefore the
  target can receive pixel output on that GPU.
- The target PS has t0 bound to a 1x1 `R8G8B8A8_UNORM` view and a sampler.
  The root-CBV bytes matched the bound D3D11 buffers. The missing
  discriminating value is the sampled texel content at draw time.
- These are GTX 1050 Ti results, not proof of the GTX 950M's behavior.

## GTX 950M v004 run — 2026-11-11

The new diagnostic DLL pair was run with the v003 fullscreen/magenta
overrides. Present #100 source and backbuffer readbacks are both uniformly
magenta: 921,600/921,600 non-black pixels each. The source resource matches
the recorded history (66 draws, zero skips, zero clears). Pipeline
statistics report 921,600 PS invocations for one override and 230,400 for
the other. The index values match between CPU shadow and D3D11 readback.

This proves the forced test path can render across the full GTX 950M
backbuffer; it does not prove that the real Hela pixel shader produces the
game image. The target PS has t0 bound to a 1x1 texture, but its texel value
was not captured. Full details and raw evidence are in
[`950m-v004-target-draw-state-20261111`](./runs/20261111/950m-v004-target-draw-state-20261111/README.md).

## GTX 950M v005 — converted pixel shaders and t0

The two converted game PS HLSL files were compiled as `ps_5_0` overrides; the
fullscreen VS override was retained and the magenta PS overrides were removed.
Both PS overrides loaded successfully. The bounded t0 readback returned
`RGBA=(255,255,255,255)`. Present #100 source and backbuffer BMPs were both
1280x720, 24-bit, and **0/921,600 pixels non-black**, with identical SHA-256
`D43EC90910E666B315661BE75DFEA0AF9722FF471AA30593999567DFFC53AC9C`.

The source history still contains 66 executed draws, zero skips, and zero
clears. The white texel rules out a black t0 as the cause of these draws'
black output. Details are in
[`950m-v005-t0-real-ps-20261111`](./runs/20261111/950m-v005-t0-real-ps-20261111/README.md).

## GTX 950M v006 — target vertex inputs

The target PSO input layout and the first four vertices were captured. Both
`ATTRIBUTE3` and `ATTRIBUTE4` decode as black RGB with alpha 1 for every
vertex. The converted PS reads `COLOR` (`ATTRIBUTE3`) for RGB, so these target
draws are expected to output black with the observed white t0 and non-zero
constant-buffer values. The full-frame BMPs again contain 0/921,600
non-black pixels.

At Present #100, all 66 draws on the matched source use VS
`20e0826b236597a1` and only PS `44b0a992c2304254` / `d77600bde0076c33`;
there are no skips or clears. By the last captured heartbeat (465 Presents),
2,126 compute PSOs had been created but no compute dispatch had executed.
This identifies the measured quad's black output, not the cause of the
missing scene. See
[`950m-v006-vertex-input-probe-20261111`](./runs/20261111/950m-v006-vertex-input-probe-20261111/README.md).

## GTX 950M v007 — white vertex color counterfactual

The v007 VS override explicitly wrote `COLOR=(1,1,1,1)` after the input
vertex color, while retaining the v006 proxy DLL pair and v005 converted PS
overrides. The log confirms the VS and both PS overrides loaded; t0 remained
white. Present #100 source and backbuffer BMPs still contained
0/921,600 non-black pixels. Thus black vertex `COLOR` alone is not sufficient
to explain the black readback. The exact DLL/CSO hashes and log are recorded
in [`950m-v007-white-vertex-color-20261111`](./runs/20261111/950m-v007-white-vertex-color-20261111/README.md).

## GTX 950M v008 — solid-white pixel shader and fingerprints

A new C++ build logs SHA-256 for the loaded d3d12/dxgi modules and each
override actually consumed. A required manifest gate checks the installed DLL
pair and exact six-file override set before launch, then checks the loaded
module and required-shader hashes in the final log. The v008 run passed both
gates.

Both target PS overrides were changed to return solid white. Present #100
source and backbuffer readbacks contain 921,600/921,600 non-black pixels,
showing the target draw can reach the source and copied backbuffer. v007 had
the same fullscreen VS positions and forced-white VS color but the converted
PSs remained black. This localizes the unresolved black output to converted PS
logic, its inputs, or translation; the forced-white frame is not normal Hela
rendering. See
[`950m-v008-solid-ps-fingerprint-20261111`](./runs/20261111/950m-v008-solid-ps-fingerprint-20261111/README.md).

## GTX 950M v009 - sampled texel times VS color

The manifest-gated run verified the v008 DLL pair and exact override set;
the log attests the expected loaded module and shader fingerprints. Both
target PS overrides returned sampled t0 RGB multiplied by interpolated
`COLOR`, with alpha forced to 1. The t0 probe read white, and pipeline
statistics confirm both target PSs executed on the Present #100 source
resource: 921,600 invocations for `44b0a992c2304254` and 230,400 for
`d77600bde0076c33`. Despite that, both source and backbuffer BMPs were
entirely black. Thus skipped target draws and the later gamma/constant-buffer
operations are not sufficient explanations. The probe did not capture the
per-pixel values actually sampled/interpolated by the PS. Next, output the
sampled texel, interpolated `COLOR`, and product to separate channels to
distinguish the inputs. Raw log, manifest, BMPs, shader sources, and
bytecodes are in
[`950m-v009-sample-color-fingerprint-20261111`](./runs/20261111/950m-v009-sample-color-fingerprint-20261111/README.md).

## GTX 950M v010 — sampled texture and COLOR channel probe

The manifest-gated run wrote `(sampled t0.r, COLOR.r, sampled t0.r * COLOR.r)`
to RGB. The t0 staging readback was white; the full source and backbuffer
captures were uniformly green, proving `COLOR.r=1` and sampled `t0.r=0` for
that capture. This confirms VS color propagation and narrows the failure to
the sampling/resource path or its timing. Details and hashes are in
[`950m-v010-input-channel-probe-20261111`](./runs/20261111/950m-v010-input-channel-probe-20261111/README.md).

## GTX 950M v011 — fixed-coordinate texture reads

The v011 manifest gate and post-run fingerprint check both passed. The two
target PSs ran on the Present #100 source with 921,600 (`44b0a992c2304254`,
`Texture2D.Load`) and 230,400 (`d77600bde0076c33`, `SampleLevel`) pixel
invocations. Both full Present #100 captures are uniformly black
(0/921,600 non-black pixels). At Present #1300 all five source and backbuffer
sample points were white. Since the late observation is only five samples
and both probes ran together, it does not prove that both operations work or
that the later full frame is white. It does show the probe output is
time-dependent. The BMPs, raw 19 MB log, launcher output, manifest, and
shader sources are archived in
[`950m-v011-fixed-texture-read-fingerprint-20261111`](./runs/20261111/950m-v011-fixed-texture-read-fingerprint-20261111/README.md).

## GTX 950M v012 — encoded `Load` probe

The isolated `Texture2D.Load` probe wrote a full-frame `(64,64,191)` at
Present #100 and `(191,191,191)` at #1300. The output encodes sampled
red/green as zero initially and one later, with blue indicating valid texture
dimensions. Periodic samples changed between #300 and #500. The t0 staging
readback was white at the first hit, so it still disagrees with the initial
shader-observed value. Both target PS overrides had identical bytecode, so
v012 does not compare the two draw hashes. This is diagnostic output, not a
game frame. See
[`950m-v012-load-encoded-fingerprint-20261111`](./runs/20261111/950m-v012-load-encoded-fingerprint-20261111/README.md).

## GTX 950M v013 — encoded `SampleLevel` probe

The `SampleLevel` probe produced a uniformly `(64,64,159)` full frame at
Present #100, consistent with sampled red/green still being zero. Both
target PSs ran, but the per-PS blue tags are not distinguishable in the final
image. The log ends at Present #800 and contains no #1300 full capture. Two
files initially copied into the v013 directory as #1300 captures were stale
v012 files (older timestamps and identical SHA-256); they have been removed.
Do not treat them as v013 evidence. A logged `CreateShaderResourceView`
failure (`0x8007000e`) is also present, but its relationship to the later
readback limit is unknown. See
[`950m-v013-samplelevel-encoded-fingerprint-20261111`](./runs/20261111/950m-v013-samplelevel-encoded-fingerprint-20261111/README.md).

## GTX 950M v014 — no-override baseline

The empty-override manifest passed the launcher gate with the v012 proxy pair.
Both full Present #100 BMPs are uniformly black; at #400 the five sampled
points remain black and the matched RTV history reports 66 skipped draws.
The log has no later readback. This is the clearest current evidence that the
real scene is not rendering in the baseline configuration. The log also
contains the same `CreateShaderResourceView` failure seen in v012/v013; it is
not yet proven to cause the skipped draws. The first launch exposed a
PowerShell 5.1 empty-JSON-object enumeration error in the launcher; property
enumeration was corrected and the rerun passed the empty-manifest and
post-run fingerprint gates. See
[`950m-v014-no-shader-override-baseline-20261111`](./runs/20261111/950m-v014-no-shader-override-baseline-20261111/README.md).

## GTX 950M v015 — managed no-override run

The new Proxy Manager validated the v015 package and manifest, launched Hela,
captured Present #400, verified both runtime DLL fingerprints, and restored
the exact pre-run installation. The v001 DLL hashes and all six pre-run shader
overrides were verified after restoration. The game process is closed.

The full-frame source and backbuffer BMPs at Presents #100 and #400 are each
1280x720 and `0/921,600` non-black. The #100 sampled readbacks are likewise
`0/100`; the #100 matched RTV history reports 0 executed and 66 skipped draws.
The final heartbeat reached Present #401 with `draw(ok)=0`, `skip-vs=947`,
`disp(ok)=0`, `PSO(G=1534,C=4019)`, and `placed(ok)=97`.

The run logged 64 `TARGET_DESCRIPTOR_WRITE` events, but zero
`CreateShaderResourceView failed`, `E_OUTOFMEMORY`, `0x8007000e`, or module
fingerprint failure events. Thus the earlier SRV/OOM failure did not reproduce
in v015; the single run does not identify which change prevented it. With no
shader overrides, the log still has zero shader-ready events and 4,957
placeholder events (256 unique retained by the monitor). This is not a
successful real-scene rendering result.

The complete run, restore point, and analysis are in
[`950m-v015-no-shader-override-srv-diagnostics-20261111-20261111-113122270`](./runs/20261111/950m-v015-no-shader-override-srv-diagnostics-20261111-20261111-113122270/README.md).

## Proxy Manager and monitor

`ProxyManager.ps1` now provides read-only inventory and package preflight,
plus a confirmed managed run and explicit restore action. Each run gets a
collision-checked UTC directory, backs up exact DLL/override bytes, archives
the run log and BMPs, binds the manifest to the live monitor, and restores the
pre-run files in `finally`. The monitor supports a configurable target Present
and reports `pending`, `verified`, `mismatch`, or `failed` module attestation.
Arena reviewed the modified tool excerpts: the process cleanup race was
actionable and is fixed with independent/nested cleanup and bounded exit waits;
the report also defensively rejects empty fingerprint sets. The review's claim
that restore was unconditional was already prevented by the `installationStarted`
guard; the manager now additionally represents initially absent proxy DLLs and
override directories and preserves compatibility with previous restore
manifests. The v015 preflight and synthetic restores passed; all 13
LiveFrameMonitor unit tests and PowerShell parser checks passed.

## Build and run identity

CMake Tools initially refused its configured build because its cache was
created under the old path
`D:\Plugin_ChatGpt\GHIDRA MCP\Plugin Nuovi\sm6-to-sm5-mcp\d3d12on11`
but was reused from
`D:\Ghidra MCP\Plugin\sm6-to-sm5-mcp\d3d12on11`. This is a stale build-tree
path mismatch, not a C++ source compile error. Configuring the current
`proxy-source` into a fresh build directory succeeded; the new fingerprint
code compiled. The previous GTX 950M CTest result remains 5/6: the one
failure is the smoke test's tiled-resource layout expectation, not CMake
configuration or compilation. The new build's full CTest suite has not yet
been run through the selected CMake Tools project.

## Run checklist

1. Keep v001 and all versioned test pairs intact; verify Hela is closed and
   confirm the installed hashes before every DLL swap.
2. Keep the manifest gate mandatory for every new run and version any changed
   proxy DLL or shader override as a distinct run artifact.
3. Investigate why target draws are skipped in the no-override baseline.
   Compare the old SRV failure in v012-v014 with v015, where it did not
   reproduce; do not infer a root cause from the one successful diagnostic run.
4. Repeat the `SampleLevel` late capture only with fresh output paths and a
   log-attested Present #1300; compare it with `Load` only after both runs
   reach that exact capture.
5. Preserve each raw log, report, BMP, and manifest; use the manager's
   verified restore point and check the restored hashes after each run.
6. Treat the current target quad as one measured draw path, not proof that all
   game rendering is black.

## Tooling decision

The existing Live Frame Monitor remains a CLI parser for the current log
format. A VS Code GUI could make live events and build comparisons easier to
inspect, but it would not answer the current draw-target/state question.
Defer the GUI until the instrumented run establishes which events and
comparisons are useful. BridgeCode reports `browserReady=true`, but a search of its 1,186-file index
did not find the current Hela readback implementation. It can still review a
carefully scoped summary, but should not be used for source-level conclusions
until its project index is refreshed. Keep the immediate effort on why the
no-override baseline skips the target draws and why the SRV creation fails;
repeat the late `SampleLevel` probe only with a fresh, log-attested capture.
