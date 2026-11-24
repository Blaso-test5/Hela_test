# Hela GTX 950M checkpoint — 2026-11-11

Raw per-run logs, binaries, shader dumps, images, and minidumps remain in the
local evidence archive and are intentionally not included in remote commits.
The checkpoint contains the summarized outcomes only.

## Current operator objective — 2026-11-24

The operator visually confirmed the pre-menu on the GTX 1050 Ti and on this
GTX 950M during v016. The 950M pre-menu reproduction goal is reached; continue
from this milestone toward the Hela menu. The v023 Shader Lab runtime run
exited before the #1400 capture and rejected six overlapping placed-resource
requests. The subsequent v024 run used a build from Hela's own `proxy-source`,
reached Present #1400, and produced fully black source/backbuffer BMPs. Keep
the two source trees and tools separate. As on the 1050 Ti, later black
samples do not revoke the operator-confirmed visible image. v025 enabled
shader dumping, captured 4,219 DXIL files, then exited with an access
violation before #1400; its crash text and minidump are archived. Analyze
those artifacts before repeating a dump-enabled run. The v024 raw log also
proves the full source-texture → CopyResource → swap-chain backbuffer →
readback pointer/sequence chain at #1400; both source and output are black.
The remaining rendering question is why the off-screen draws produce black
pixels, not whether the copy or readback ran. See the v024 raw proxy log (local-only: `runs/20261123/950m-v024-hela-native-uav-device-removed-diagnostic-213458199/d3d12on11.log`).

Use only a separately versioned `950m-*` profile, package, output directory,
and runtime/shader attestations. Do not copy the 1050 Ti hashes as 950M
evidence. Resume from this checkpoint and the archived `v001`–`v016` results;
do not repeat an equivalent probe unless a specific changed variable or
validation need is recorded in a new test version. Preserve all prior
negative, incomplete, and failed runs.

## Latest continuation — v024 Hela-native run (2026-11-23)

The v024 staged-only package passed both its package gate and
`ProxyManager -Action Prepare`; it contains the Hela-native runtime and all 14
baseline override files. Runtime hashes:

- `d3d12.dll`: `397918b24ad94aa32a6a2a7ee5b6b4e36dcb97063d91f395624c8a5adf6c6bae`
- `dxgi.dll`: `f3d0a95299e1151334666105dc88895c684d5fe95f426ad45b3bc7b152f5fd10`

The Hela-source CMake build succeeded. CTest passed 7/8; only
`d3d12on11_smoke` failed, reporting `tiles=1 packed-mips=0 subresources=1`
where its assertion expects one packed mip. Keep this failure visible and
investigate the assertion/feature behavior; do not confuse it with the
separate Shader Lab CTest results.

The v024 archived run (local-only: `runs/20261123/950m-v024-hela-native-uav-device-removed-diagnostic-213458199/`)
passed runtime attestation and completed the Present #1400 capture. Both
full-frame readbacks were `0/921,600` non-black; the verifier returns
`present-reached-black-readback`, not menu validation. Present #100 sampled
11/100 non-black points. Three graphics pairs executed on off-screen RTVs.
The source texture copied at sequence 1400 (`0000022027A84578`) has 800
recorded draws. The raw log matches its copy source pointer and sequence to
the readback, and matches the copy destination wrapper to the native
swap-chain backbuffer. Both full-frame BMPs are black, proving that the
black pixels exist before the copy. The generic evidence report still marks
14 correlations candidate-only; the v024 raw pointer/sequence chain directly
confirms this final-frame path, but not which draw/shader generated black
pixels. The log has 5,839 placeholders and 13 ready SM5 overrides, with no
native SM5 shaders. Hela's placed-resource requests succeeded; the six
overlap rejections from v023 did not recur. The new failed-UAV
`GetDeviceRemovedReason` diagnostic was not exercised.

The launcher requested shutdown after the target capture and force-terminated
the process during cleanup; do not interpret this as a spontaneous crash.
`manager-run.json` records `restored=true`; post-run inventory confirmed the
pre-run DLL hashes and all six installed overrides. Consolidated evidence is
in the v024 offline-evidence directory (local-only: `runs/20261123/profiles/950m-v024-hela-native-build-agent-20261123/run-evidence/`).

Next, preserve the v024 result as a black-rendered-source failure. Inspect
the inputs and outputs of the executed draws on that source to find where
non-black pixels are lost. Investigate the tiled-resource smoke assertion
separately. Do not merge, move, or remove either proxy tree.

## Latest diagnostic trace — v025 shader dump (2026-11-23)

The v025 package (local-only: `runs/20261123/profiles/950m-v025-hela-native-shader-dump-20261123/package/`)
passed the staged-only package gate and read-only manager preflight. It uses
the same Hela-native DLL pair and shader baseline as v024; only
`D3D12ON11_DUMP_SHADERS` changed to `1`.

The archived run (local-only: `runs/20261123/950m-v025-hela-native-shader-dump-20261123-214230728/`)
captured 4,219 `.dxil` files totaling 47,486,892 bytes. Its log reports 4,236
unique shaders, 4,218 placeholders, 13 ready SM5 overrides, 140/142 graphics
PSOs with placeholders, and 227/227 compute PSOs with placeholders. Three
graphics pairs executed; all were observed off the backbuffer RTV. The
source/backbuffer samples were black by #1000 and remained 0/100 through the
last readback #1300. The target #1400 readback was not reached. This run does
not supersede v024's complete pointer/sequence chain; v025 adds shader
bytecode and crash artifacts, but does not identify why the source renders
black.

Hela exited before monitor cleanup with `0xC0000005` (read access to address
zero), reported at `Hela-Win64-Shipping.exe+0x25C2872`. The crash handler
saved
crash report (local-only: `runs/20261123/950m-v025-hela-native-shader-dump-20261123-214230728/crash-diagnostics/d3d12on11-crash-26048-20261123-224250.txt`)
and
minidump (local-only: `runs/20261123/950m-v025-hela-native-shader-dump-20261123-214230728/crash-diagnostics/d3d12on11-crash-26048-20261123-224250.dmp`).
The report has only module offsets; no WinDbg MCP session or local Hela PDB
was available to symbolize them. The crash occurred during the dump-enabled
run, but causation is not established.

`ProxyManager` reports `restored=true`; current inventory matches the v024
pre-run runtime hashes and confirms Hela is closed. The consolidated
offline evidence (local-only: `runs/20261123/profiles/950m-v025-hela-native-shader-dump-20261123/run-evidence/`)
records that #1400 is missing and menu verification remains unverified.
Next, analyze the captured bytecode and minidump before any repeat. Use v024
to investigate the off-screen source's executed draw inputs and output, rather
than treating the confirmed copy/present path as suspect. Missing shader
translations remain a broader compatibility issue; they are not yet proven
to be the cause of the black output from this source.

For each shader, maintain its exact hash and stage, run/version, override
hash, and evidence path. Promote it only through the measured states
`cataloged` → `ready` → `executed` → `target-correlated` →
`pixel-validated`. A loaded or executed override alone is not proof that it
contributes to the visible pre-menu. The first 950M success criterion is an
operator-confirmed visible pre-menu with the run's GPU, runtime, shader
attestation, launch profile, and restoration state recorded; v016 meets that
criterion. Its Present #1400 capture is incomplete, so do not mark the shader
set pixel-validated from this run.

## Goal and status

The v016 operator-confirmed pre-menu is the first visible-image milestone on
the GTX 950M. The next goal is to reach and verify the Hela menu. Earlier
v012/v013 encoded probes remain diagnostic-only, and v014/v015 remain
negative no-override baselines; preserve them as historical evidence.

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
corrected. On 2026-11-22, Visual Studio 18 / MSVC 19.50.35721.0 compiled all
34 C++ build steps, including the proxy DLLs, tests, and `nvapi_trace`.
CTest passed 6/8 tests. `d3d12on11_smoke` failed its tiled-resource layout
expectation (`tiles=1, packed-mips=0, subresources=1`); the shader-use-ledger
smoke failed its expected execution count at `shader_use_ledger_smoke.cpp:39`.
The v016 run therefore used the previously attested 950M v015 runtime, not
the fresh build. Temporary build output stayed outside the repository.

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
path mismatch, not a C++ source compile error. A fresh out-of-tree build of
the current `proxy-source` compiled all 34 targets successfully. Its CTest
result is 6/8, with the two failures described above. The v016 package instead
used the previously run-attested v015 DLL pair, whose fingerprints matched
during the run.

## Run checklist

1. Keep v001 and all versioned test pairs intact; verify Hela is closed and
   confirm the installed hashes before every DLL swap.
2. Keep the manifest gate mandatory for every new run and version any changed
   proxy DLL or shader override as a distinct run artifact.
3. Keep v016 as the first user-confirmed 950M pre-menu milestone. Preserve
   its incomplete Present #1400 capture and verified restore; do not repeat
   the same shader test without a concrete reason.
4. Proceed to a separately versioned test aimed at the Hela menu, starting
   from the v016 package and its run evidence.
5. If a full Present #1400 capture is later required, use a longer monitor
   timeout in a new run attempt; keep the current incomplete attempt intact.
6. Preserve each log, report, BMP, manifest, and restore point. Re-authorize
   online runs separately and keep the TCP metadata trace local.

## Tooling decision

The existing Live Frame Monitor and ProxyManager were sufficient for v016;
no ProxyManager code change was needed. Keep future work on the next visual
milestone—the Hela menu—and use the v016 evidence as the 950M pre-menu
checkpoint.

## GTX 950M v016 — 1050 Ti shader-set comparison

The user confirmed that the Hela pre-menu was visibly rendered on the GTX
950M. The new 950M-specific package reused the v015 runtime DLLs and the 14
CSOs from the known 1050 Ti v1.1.3 package; all 13 required overrides were
reported ready. The 950M launch profile used Tier 0. Runtime fingerprints
matched the v016 manifest, and the three shaders correlated with the Present
#100 source history were VS `20e0826b236597a1` and PS
`44b0a992c2304254` / `d77600bde0076c33`.

At Present #100, source and backbuffer samples each reported 9/100
non-black pixels; the matched source history had 66 draws, zero skips, and
zero clears. Five-point samples from Presents #300–1000 were black. The
monitor did not reach the requested full source/backbuffer readbacks at
Present #1400, so ProxyManager marked the attempt failed for that missing
capture, not for the visual result. The user-confirmed milestone is valid;
this run is not full-frame pixel validation.

ProxyManager recorded `restored: true`; post-run checks matched the exact
pre-run v001 DLL hashes and six override files, and Hela was stopped. The
online TCP-only observer recorded two Hela connection-state events without
capturing payload or UDP. Full run evidence is in
v016 run report (local-only: `runs/20261122/950m-v016-pre-menu-1050ti-shader-set-20261122-182755274/README.md`).

Do not repeat v016 just to reach #1400. Continue toward the Hela menu with a
newly versioned 950M test; use a longer monitor timeout only if a complete
#1400 capture is needed for a specific diagnostic question.

## GTX 950M v017-v019 — device-removal diagnosis (2026-11-22)

The v016, v017, and v018 manifests attest identical v015 runtime DLL hashes,
shader overrides, and launch-profile hash. The runs do not provide a
runtime-by-shader comparison and cannot implicate the shader set by
themselves. See the v017 manifest (local-only: `runs/20261122/profiles/950m-v017-menu-progression-1050ti-shaders-20261122/run-manifest.json`),
v017 log (local-only: `runs/20261122/950m-v017-menu-progression-1050ti-shaders-20261122-184749626/d3d12on11.log`),
v018 manifest (local-only: `runs/20261122/profiles/950m-v018-startup-crash-diagnostics-20261122/run-manifest.json`),
and v018 log (local-only: `runs/20261122/950m-v018-startup-crash-diagnostics-20261122-185839186/d3d12on11.log`).

The v017 log ends with `0x887A0005` from resource backing creation, followed
by `CreateQuery` and `Signal` failures with that HRESULT. In v018, the
heartbeat reached Present #1313 at about 18 seconds, with 2,580 draws and 21
dispatches. The last logged failures are `CreatePlacedResource`,
`CreateQuery`, and `Signal` with `0x887A0005` (`DXGI_ERROR_DEVICE_REMOVED`).
This confirms device removal was reported by the runtime, but does not
identify the initiating fault or establish that a TDR occurred.

Although the manager recorded crash diagnostics enabled for v018, the runtime
logged `crash-diagnostics=0`. The launcher's profile branch cleared inherited
`D3D12ON11_*` variables and did not add `D3D12ON11_CRASH_DIAGNOSTICS` back to
the child environment. This explains how the manager's `true` setting and
the runtime's `0` could coexist; v018 used the old v015 runtime pair and did
not validate the updated diagnostic build. No exception stack or minidump
was captured.

Present #1400 was not reached; the manager restored the prior files. Neither
the manager report nor the monitor report records Hela's exit code. The proxy
log stopping after about 19 seconds does not prove Hela exited then: the
monitor is bounded at 180 seconds, and its launcher closes Hela during cleanup
if the game is still running after the monitor ends. The manager's 362-second
wall interval includes monitoring and cleanup; it is not Hela's measured
uptime. The report cannot distinguish a crash, a hang, or cleanup termination.

The launcher now explicitly propagates `D3D12ON11_CRASH_DIAGNOSTICS` through
profile launches and writes `process-status.json` with the game's exit code
and whether cleanup requested or forced termination.

A read-only Windows event-log check found no matching display-reset or
application-crash event. It found one corrected PCIe AER warning at 19:59:48
on Intel root port `0:1C:4`; the currently enumerated NVIDIA adapter reports
a different PCI location path, so this is not evidence that the warning
caused the NVIDIA device removal.

### v019 process and device-removal evidence

The v019 manifest (local-only: `runs/20261122/profiles/950m-v019-device-removal-diagnostics-20261122/run-manifest.json`)
pins the new diagnostic runtime and shader set. Runtime module fingerprints
matched the manifest, and the log reports `crash-diagnostics=1`, verifying the
v018 environment-propagation fix.

At 21:25:36.798 the proxy logged
`CreateUnorderedAccessView result=0x887a0005`
(`DXGI_ERROR_DEVICE_REMOVED`). Its last heartbeat was at 21:25:56.534:
Present #629, runtime uptime 52.015 seconds, with `Dispatch` as the last API.
The Present #1400 monitor target was not reached. Present #100 source and
backbuffer samples each had 11/100 non-black pixels; no full-frame result is
available. No `GetDeviceRemovedReason` value was emitted for the failing
operation.

The process-status report (local-only: `runs/20261122/950m-v019-device-removal-diagnostics-20261122-202451048/process-status.json`)
shows Hela had not exited before cleanup. The monitor exited first; after the
60-second bound the launcher requested shutdown and force-terminated the game.
The exit code before cleanup is null; the post-cleanup value `-1` followed
forced termination and cannot establish the game's natural exit code.
Therefore this run does not demonstrate a spontaneous game crash; the manager
marked it failed because Present #1400 was incomplete.
Windows event logs contained no display-reset (`4101`), `nvlddmkm`,
Application Error (`1000`), or WER (`1001`) event during the run.

ProxyManager recorded `restored=true`. Independent SHA-256 checks confirmed
that both runtime DLLs and all six installed shader overrides match the
v019 restore manifest. See the
manager report (local-only: `runs/20261122/950m-v019-device-removal-diagnostics-20261122-202451048/manager-run.json`),
monitor report (local-only: `runs/20261122/950m-v019-device-removal-diagnostics-20261122-202451048/live-monitor-report.json`),
and proxy log (local-only: `runs/20261122/950m-v019-device-removal-diagnostics-20261122-202451048/d3d12on11.log`).

### Next diagnostic gate

Do not repeat the v016-v019 runs with the same runtime and shader set. The
v019 run verifies that the new runtime and crash-diagnostics flag were loaded,
but the specific `CreateUnorderedAccessView` failure still has no logged
`GetDeviceRemovedReason`. The removal's initiating cause is unknown; the
observed HRESULT alone does not establish a TDR. Preserve the v019 artifacts
and restore manifest.

### v020 staged-only package and manager gate (2026-11-23)

The operator resumed offline preparation. Shader Lab 0.1.9 generated the
staged package at
950m-v020-shaderlab-0.1.9-package-20261123 (local-only: `runs/20261123/profiles/950m-v020-shaderlab-0.1.9-package-20261123/package/`).
The package gate passed with all 14 baseline overrides preserved and zero new
overrides included. `ProxyManager -Action Prepare` accepted the staged-only
attestation with `ready=true` and `mutation_performed=false`. A fail-closed
preflight rejected the package after `candidate-package.json` was removed;
the installed runtime DLL hashes were unchanged.

This package reuses the v019 `d3d12.dll` and `dxgi.dll`; it is not a new proxy
runtime test. No game launch or installation was performed. Shader Lab's
`d3d12on11` source remains separate from Hela's `proxy-source`. The failed-UAV
`GetDeviceRemovedReason` log was added and the proxy built successfully with
Visual Studio 18 2026 in an isolated `build-agent` directory.

All eight registered CTest smoke tests pass. The two initial failures were
test assumptions: the shader-use test expected a different order than the
diagnostic-priority sort, and the tiled-resource test required a packed mip
even though Microsoft documents that smaller mips may or may not be packed.
The assertions now reflect those contracts. This validates the local build
and smoke suite, but not a Hela run or equivalence with Hela's separate
`proxy-source`. Do not deploy the DLLs or launch Hela until the two proxy
sources are reconciled and a new package is staged and reviewed.

### v023 Shader Lab runtime package and manager gate (2026-11-23)

The v023 package (local-only: `runs/20261123/profiles/950m-v023-shaderlab-build-agent-20261123/package/`)
uses the Shader Lab `build-agent` DLLs:
`d3d12.dll` SHA-256
`27b8f2a31f64b89511b41fce00d0b23b023674c9c6117953d8e7ef4c0bf491b0`,
`dxgi.dll` SHA-256
`161ca838f5fbe3747e8b42aa7d00272f7e46f3ea7650e064ae7c008cfd066eb2`.
Its gate passed with all 14 baseline overrides preserved, three candidates
compiled, one VS/PS pair validated, three candidates blocked by baseline
conflicts, and zero new overrides included. `ProxyManager -Action Prepare`
accepted it with `ready=true`, `lifecycle=staged-only`, and
`mutation_performed=false`.

The package was built against a separate seed manifest derived from v020's
shader baseline and pinned to the new DLL hashes; the original v020 manifest
was not changed. This clears packaging and read-only preflight only; it does
not establish runtime equivalence or rendering.

### v023 Shader Lab runtime run (2026-11-23)

The v023 run directory (local-only: `runs/20261123/20261123/950m-v023-shaderlab-build-agent-20261123-212500745/`)
preserves the full run evidence. Both loaded runtime hashes matched the
manifest and crash diagnostics were enabled. Hela exited before cleanup with
code `16384` (`0x4000`); neither the launcher nor monitor recorded a launcher
shutdown request, and the monitor did not capture Present #1400.

At Present #100, the matched source and backbuffer samples each had 11/100
non-black pixels; source history recorded 66 draws, zero skips, and zero
clears. The last heartbeat reached Present #782 at about 42 seconds. The
proxy log ends with six rejected overlapping `CreatePlacedResource` requests
(`0x80070057`); there is no device-removed HRESULT or captured crash report.
The overlap difference from Hela's source is a plausible contributor, not
proven as the exit cause. This run does not establish menu rendering or
pixel-validation.

`ProxyManager` reports `restored=true`, and independent checks matched the
pre-run hashes for both installed DLLs and all six overrides. Do not repeat
this Shader Lab runtime on Hela. Continue using the separate Hela
`proxy-source` and preserve its accepted placed-resource alias behavior; do
not merge or move either proxy tree during graphics-compatibility work.
