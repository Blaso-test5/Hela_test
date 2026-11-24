# GTX 950M Hela test

Initial run date: 2026-11-11; latest update: 2026-11-24

Raw per-run logs, binaries, shader dumps, images, and minidumps remain in the
local evidence archive and are intentionally not included in remote commits.
This document publishes the summarized outcomes only.

For the latest continuation state and next diagnostic run, see
[`checkpoint-20261111.md`](./checkpoint-20261111.md).

Current objective (2026-11-24): the pre-menu has now been visibly reproduced
on the GTX 950M in v016, as confirmed by the operator. Continue toward the
Hela menu and subsequent tests. Use a distinct 950M version/profile and resume
from the latest checkpoint without repeating equivalent tests.

This directory documents Hela tests on this PC's NVIDIA GeForce GTX 950M
(device `0x139A`, driver `582.78`, 4 GiB). The v016 package reuses the
identified shader files from the GTX 1050 Ti reference run.

## GTX 950M v016 — visible pre-menu milestone

The user visually confirmed the pre-menu on the GTX 950M while running the
exact 1050 Ti v1.1.3 shader set in a separate 950M profile. All 13 required
shader overrides reported `SHADER READY`; runtime fingerprints matched the
950M v015 DLLs. At Present #100, source and backbuffer samples each reported
9/100 non-black pixels, with 66 draws and no skips or clears in the matched
source history. The target shaders were VS `20e0826b236597a1` and PS
`44b0a992c2304254` / `d77600bde0076c33`.

The monitor timed out before the requested Present #1400 full readbacks, so
ProxyManager marked the run incomplete for that capture. It did restore the
pre-run v001 DLLs and all six installed overrides, which were independently
verified. Treat the visual milestone as achieved, but do not label the full
shader set pixel-validated. See the
v016 run report (local-only: `runs/20261122/950m-v016-pre-menu-1050ti-shader-set-20261122-182755274/README.md`)
and [checkpoint](./checkpoint-20261111.md).

## GTX 950M v017-v018 — device-removal evidence

The v016-v018 manifests attest the same v015 runtime DLL hashes, the same
shader overrides, and the same launch-profile hash. These runs therefore do
not isolate a shader-set change as the cause of the later failure.
Evidence: v017 manifest (local-only: `runs/20261122/profiles/950m-v017-menu-progression-1050ti-shaders-20261122/run-manifest.json`),
v017 proxy log (local-only: `runs/20261122/950m-v017-menu-progression-1050ti-shaders-20261122-184749626/d3d12on11.log`),
v018 manifest (local-only: `runs/20261122/profiles/950m-v018-startup-crash-diagnostics-20261122/run-manifest.json`),
and v018 proxy log (local-only: `runs/20261122/950m-v018-startup-crash-diagnostics-20261122-185839186/d3d12on11.log`).

The v017 proxy log ends with `0x887A0005` from resource backing creation,
followed by `CreateQuery` and `Signal` failures with the same HRESULT. In v018,
the heartbeat reached Present #1313 at about 18 seconds (2,580 draws and 21
dispatches); the log then records `0x887A0005` during `CreatePlacedResource`,
`CreateQuery`, and `Signal`. This is `DXGI_ERROR_DEVICE_REMOVED` and confirms
device removal was reported by the runtime. It does not identify why the
device was removed or prove a TDR.

The manager marked crash diagnostics enabled for v018, but the loaded runtime
logged `crash-diagnostics=0`. The launcher profile branch cleared inherited
`D3D12ON11_*` variables and did not add `D3D12ON11_CRASH_DIAGNOSTICS` back to
the child environment; this explains how the manager's `true` setting and the
runtime's `0` could coexist. The run used the old v015 runtime pair and did
not validate the updated diagnostic build. No exception stack or dump was
captured.

The readback target at Present #1400 was not reached. Neither the manager
report nor the monitor report records Hela's exit code. The proxy log stopping
after about 19 seconds does not prove that Hela exited then: the monitor has a
180-second bound, and its launcher closes Hela during cleanup if it is still
running after the monitor ends. The report therefore cannot distinguish a
game crash from a hang or cleanup termination. The launcher now explicitly
propagates the diagnostics flag through profile launches and writes
`process-status.json` with the game's exit code and whether cleanup requested
or forced termination. ProxyManager did restore the prior files.

A read-only Windows event-log check found no matching display-reset or
application-crash event. It found one corrected PCIe AER warning on Intel
root port `0:1C:4`, while the currently enumerated NVIDIA adapter has a
different PCI location path. That warning does not establish the cause of the
NVIDIA device removal.

Do not repeat v016-v018 with the same runtime and shader set. The v019 run
below used a separately attested runtime and recorded the process cleanup
status.

## GTX 950M v019 — device removal and launcher cleanup (2026-11-22)

The v019 manifest (local-only: `runs/20261122/profiles/950m-v019-device-removal-diagnostics-20261122/run-manifest.json`)
pins the diagnostic runtime and shader set. The loaded `d3d12.dll` and
`dxgi.dll` fingerprints matched the manifest, and the proxy logged
`crash-diagnostics=1`; the v018 flag-propagation gap is resolved.

The proxy reported `CreateUnorderedAccessView result=0x887a0005`
(`DXGI_ERROR_DEVICE_REMOVED`) at 21:25:36.798. The final heartbeat, at
21:25:56.534, was Present #629 (`up=52015ms`) with `Dispatch` as the last API.
The monitor's requested Present #1400 capture remained incomplete. At Present
#100, source and backbuffer each sampled 11/100 non-black pixels; this is not
a full-frame validation. No `GetDeviceRemovedReason` value was logged for the
failure.

The process status (local-only: `runs/20261122/950m-v019-device-removal-diagnostics-20261122-202451048/process-status.json`)
shows that Hela had not exited before cleanup. The monitor had exited; the
launcher then requested shutdown and force-terminated Hela after the
60-second bound. Thus the manager's failed status reflects the incomplete
#1400 capture, and this run does not establish a spontaneous game-process
crash. The process exit code before cleanup was unavailable. The read-only
Windows event check found no display-reset (`4101`), `nvlddmkm`, Application
Error (`1000`), or WER (`1001`) event in the run interval.

ProxyManager reported `restored=true`; post-run hashes for both runtime DLLs
and all six installed shader overrides matched the restore manifest. The
manager report (local-only: `runs/20261122/950m-v019-device-removal-diagnostics-20261122-202451048/manager-run.json`),
monitor report (local-only: `runs/20261122/950m-v019-device-removal-diagnostics-20261122-202451048/live-monitor-report.json`),
and proxy log (local-only: `runs/20261122/950m-v019-device-removal-diagnostics-20261122-202451048/d3d12on11.log`)
preserve the run evidence.

The device-removal trigger remains unknown; `DXGI_ERROR_DEVICE_REMOVED` alone
does not prove a TDR. Offline staging resumed after v019. v020 reused the
v019 runtime; v023 packaged and tested the separate Shader Lab build. The v023
run was incomplete and exposed rejected placed-resource overlaps. Use Hela's
own `proxy-source` for the next compatibility build. See the
[updated checkpoint](./checkpoint-20261111.md).

## GTX 950M v020 — staged package and manager gate (2026-11-23)

Shader Lab 0.1.9 generated a staged-only package at
950m-v020-shaderlab-0.1.9-package-20261123 (local-only: `runs/20261123/profiles/950m-v020-shaderlab-0.1.9-package-20261123/package/`).
The package gate passed. It preserves all 14 baseline overrides, processed
seven DXIL files, compiled three candidates, pair-validated one, blocked
three because of baseline conflicts, and included zero new overrides.

The package still carries the v019 runtime: `d3d12.dll`
`31efa68c8c2ce7915ef4d4f6e5395e946ab478b94a0192ffe960b318f417a3f9` and
`dxgi.dll` `81279a4fd95b1acabf23dd6e4b3f33cc6d142a899cfda0ace340fbe97bd45697`.
`ProxyManager -Action Prepare` accepted the staged-only attestation
(`ready=true`, `mutation_performed=false`). A negative preflight with
`candidate-package.json` removed was rejected, and independent SHA-256 checks
confirmed the installed runtime DLLs were unchanged.

No game run or installation was performed. The new Shader Lab
[`d3d12on11` source](../../../../../Plugin/sm6-to-sm5-mcp/d3d12on11/) is still
separate from Hela's `proxy-source`; v020 does not use it. A diagnostic log
for `GetDeviceRemovedReason` was added to that source's failed UAV path. The
proxy now builds with Visual Studio 18 2026 in an isolated `build-agent`
directory. All eight registered CTest smoke tests pass. The two initial
failures were test assumptions: the shader-use test expected a different
ordering from the diagnostic-priority sort, and the tiled-resource test
required a packed mip even though Microsoft documents that smaller mips may
or may not be packed. The assertions now match those contracts. This
validates the local build and smoke suite, but not a Hela run or equivalence
with Hela's separate `proxy-source`. Do not install the DLLs or launch Hela
until the two proxy sources are reconciled and a new package is staged and
reviewed.

## GTX 950M v023 — Shader Lab runtime staged (2026-11-23)

The v023 staged-only package is at
950m-v023-shaderlab-build-agent-20261123 (local-only: `runs/20261123/profiles/950m-v023-shaderlab-build-agent-20261123/package/`).
It packages the freshly built Shader Lab runtime:
`d3d12.dll` SHA-256
`27b8f2a31f64b89511b41fce00d0b23b023674c9c6117953d8e7ef4c0bf491b0` and
`dxgi.dll` SHA-256
`161ca838f5fbe3747e8b42aa7d00272f7e46f3ea7650e064ae7c008cfd066eb2`.
The package gate passed with 14 baseline overrides preserved; three
shader candidates were compiled, one pair-validated, three blocked by
baseline conflicts, and zero new overrides included.

`ProxyManager -Action Prepare` accepted the package
(`ready=true`, `mutation_performed=false`, `lifecycle=staged-only`). The
candidate uses the existing v020 shader baseline with a separate seed
manifest pinning the new DLL hashes; the v020 baseline manifest remains
unchanged. This validates package provenance and preflight, not rendering or
equivalence with Hela's `proxy-source`. The subsequent v023 live run is
recorded below. Keep Hela's source and tools separate; do not remove or merge
its proxy while compatibility work is ongoing.

### v023 live run — incomplete capture, automatic restore

The v023 run directory (local-only: `runs/20261123/20261123/950m-v023-shaderlab-build-agent-20261123-212500745/`)
contains the manager, process, monitor, manifest, and proxy-log evidence.
Loaded `d3d12.dll` and `dxgi.dll` hashes both matched the v023 package, and
the log confirms `crash-diagnostics=1` with the first-chance handler
installed. Hela exited before launcher cleanup with exit code `16384`
(`0x4000`); the launcher did not request shutdown or force termination. The
monitor timed out before the requested Present #1400 source/backbuffer
readbacks, so the manager marked the run failed/incomplete.

At Present #100, source and backbuffer both sampled 11/100 non-black pixels.
The matched source history recorded 66 draws, zero skips, and zero clears;
the three target graphics shaders were ready. The last heartbeat, at about
42 seconds, recorded Present #782, 1,339 draws, and 21 dispatches. The log
contains 256 unique shader placeholders (5,818 events). No device-removed
HRESULT, crash report, Application Error/WER event, or display-reset event was
captured.

Near the time Hela exited, the Shader Lab runtime rejected six overlapping
`CreatePlacedResource` requests with `0x80070057` (`E_INVALIDARG`). Hela's
existing proxy source instead accepts placed-range aliases with independent
D3D11 backing. This is a material compatibility difference and a plausible
contributor to the early exit, but the evidence does not prove causation.
Do not repeat this Shader Lab runtime on Hela. Continue with a build from
Hela's own `proxy-source`, preserving its placed-resource behavior and the
v023 shader baseline.

`ProxyManager` reported `restored=true`. Independent SHA-256 verification
confirmed both runtime DLLs and all six installed overrides match the
pre-run restore manifest. The test did not reach the Hela menu and does not
validate the full shader set.

## GTX 950M v024 — Hela-native proxy run (2026-11-23)

The v024 package is at
950m-v024-hela-native-build-agent-20261123 (local-only: `runs/20261123/profiles/950m-v024-hela-native-build-agent-20261123/package/`).
It was built from Hela's own `proxy-source`, retaining its placed-resource
alias handling; the failed-UAV path now also logs `GetDeviceRemovedReason`.
The runtime DLL hashes are `d3d12.dll`
`397918b24ad94aa32a6a2a7ee5b6b4e36dcb97063d91f395624c8a5adf6c6bae` and
`dxgi.dll`
`f3d0a95299e1151334666105dc88895c684d5fe95f426ad45b3bc7b152f5fd10`.
Staging and `ProxyManager -Action Prepare` both passed without modifying the
installation. The package preserves all 14 baseline override files and adds
no new shader overrides.

The Hela-source build succeeded, but its CTest suite passed 7/8: the
`d3d12on11_smoke` test reported
`tiles=1 packed-mips=0 subresources=1` against an assertion expecting one
packed mip. This test remains unresolved; the separate Shader Lab test result
does not apply to this Hela-source build.

The v024 run (local-only: `runs/20261123/950m-v024-hela-native-uav-device-removed-diagnostic-213458199/`)
reached and completed Present #1400 with runtime attestation passing. Both
full-frame source and backbuffer BMPs were completely black
(`0/921,600` non-black pixels); the read-only verifier classified the result
as `present-reached-black-readback`, with `menuValidated=false`. At Present
#100 both sampled readbacks showed 11/100 non-black points. Three graphics
pairs executed on off-screen RTVs. Raw log pointers and copy sequence prove
that this is the path reaching the backbuffer at #1400: source texture
`0000022027A84578` has 800 recorded draws; `CopyResource` sequence 1400 copies
it to the swap-chain resource; the readback records that same source pointer,
destination backbuffer pointer, and sequence. Both full-frame readbacks are
black. Thus copy/present/readback is verified, and the black pixels are
already present in the rendered source before the copy. The generic evidence
report still labels its 14 correlations candidate-only; the raw v024 proxy log (local-only: `runs/20261123/950m-v024-hela-native-uav-device-removed-diagnostic-213458199/d3d12on11.log`)
provides stronger pointer/sequence evidence, but it does not identify
which draw or shader produces the black pixels. The run counted 5,839 shader
placeholders, 13 ready SM5 overrides, and zero native SM5 shaders.

Unlike v023, the Hela-native run showed successful placed-resource creation
and no matching `E_INVALIDARG` overlap failures. No failed-UAV event exercised
the new device-removal diagnostic. The launcher requested shutdown after the
target capture and then forced process termination during cleanup; this is
not evidence of a spontaneous crash. `ProxyManager` reported
`restored=true`; post-run inventory confirmed the original runtime hashes
(`d3d12.dll` `209c78ca0fa244228a967a6a98465cd625a92d1696fe6d1a68edc45cfdeaf062`,
`dxgi.dll` `06f0836565bfc577956bb4cb2540e104c94de1fb68ac6fba5283d8082dfdc2e2`)
and the same six installed overrides.

Consolidated offline evidence is in
run-evidence (local-only: `runs/20261123/profiles/950m-v024-hela-native-build-agent-20261123/run-evidence/`);
raw run data and both #1400 BMPs remain in the run directory above. Next,
inspect the executed source-target draws and their inputs/outputs to locate
where non-black pixels are lost; do not assume the backbuffer copy is at fault.
Investigate the tiled-resource smoke assertion separately. Keep using Hela's
separate proxy source and do not merge or move either proxy tree.

## GTX 950M v025 — shader dump and crash trace (2026-11-23)

The v025 staged package (local-only: `runs/20261123/profiles/950m-v025-hela-native-shader-dump-20261123/package/`)
uses the same Hela-native runtime and baseline overrides as v024; its only
launch-profile change is `D3D12ON11_DUMP_SHADERS=1`. Package and
`ProxyManager -Action Prepare` gates passed without installing anything.

The archived run (local-only: `runs/20261123/950m-v025-hela-native-shader-dump-20261123-214230728/`)
captured 4,219 DXIL files (47,486,892 bytes) before Hela exited with
`0xC0000005` at Present #1324. The monitor reached readbacks through #1300,
but not the requested #1400; the offline verifier returns
`present-not-reached`. Sampled source and backbuffer pixels were both
11/100 at #100-300, 0/100 at #400, 3/100 at #500-600, 4-5/100 at #700-900,
and 0/100 at #1000-1300.

The shader log records 4,236 unique shaders: 4,218 placeholders, 13 ready
SM5 overrides, and no blocked/failed shader operations. 140/142 graphics
PSOs and all 227 compute PSOs were placeholders. Three graphics pairs
executed (1,323, 992, and 274 draws), but none was observed on a backbuffer
RTV. The v025 run ended before #1400, so its correlations remain
candidate-only; use the complete v024 pointer/sequence chain above for the
confirmed source-to-backbuffer path. The dump confirms substantial missing
shader translation, but does not identify which shader or input makes the
source pixels black.

The crash handler preserved a text report (local-only: `runs/20261123/950m-v025-hela-native-shader-dump-20261123-214230728/crash-diagnostics/d3d12on11-crash-26048-20261123-224250.txt`)
and minidump (local-only: `runs/20261123/950m-v025-hela-native-shader-dump-20261123-214230728/crash-diagnostics/d3d12on11-crash-26048-20261123-224250.dmp`).
The report identifies a read access violation at null address in
`Hela-Win64-Shipping.exe` offset `0x25C2872`; its stack contains executable
offsets only. There was no active WinDbg MCP session or local Hela PDB, so
the fault is not symbolized and its relation to shader dumping is unproven.

`ProxyManager` records `restored=true`; post-run inventory confirmed the same
pre-run DLL hashes as v024 and Hela is no longer running. Consolidated
evidence is in
run-evidence (local-only: `runs/20261123/profiles/950m-v025-hela-native-shader-dump-20261123/run-evidence/`).
Do not repeat the dump-enabled run unchanged. Analyze the captured DXIL and
minidump first; retain the v024 completed black #1400 readback and its
source-to-backbuffer pointer chain as the current full-frame result.

## Cross-machine handoff: GTX 1050 Ti run

The versioned GTX 1050 Ti pre-menu run and its sanitized results were pushed
to `main` in commit `e3c4146`. Read the
[GTX 1050 Ti findings](../GTX_1050TI/README.md) and
[v1.1.2 post-run validation](../GTX_1050TI/runs/20261119/1050ti-pre-menu-v1.1.2-historical-env-181838503/post-run-validation.json)
for context: the user saw the pre-menu, but both full-frame readbacks were
black, so the pixels do not independently verify the visible image.

**Do not run the `1050ti-*` profile or launcher on the GTX 950M, and do not
store its output under this directory.** That profile is specific to the
GTX 1050 Ti run. On the 950M machine, use these files only to read the prior
results; any continuation must first verify the local GPU and runtime, then
use a separately versioned 950M profile and output path.

### 1050 Ti diagnostic facts to reproduce on the 950M

The 1050 Ti v1.1.3 resource-snapshot run reached Present #1400 and preserved
snapshot history for every 100th Present. It found three source/RTV resources
and zero skipped draws or clears in those resource histories. Pixel samples
fell from 11/100 at Present #100 to 2/100 at Present #300-400 and 0/100 from
Present #500 onward; the full Present #1400 source and backbuffer BMPs were
100% black.

The most important 1050 Ti pipeline finding was that the target history
executed 1,472 sampled draw events using only these pairs:

- `20e0826b236597a1` / `44b0a992c2304254`
- `20e0826b236597a1` / `d77600bde0076c33`
- `4ce363172cfd70be` / `4c1bc8bf7e457acd`

No new placeholder or missing-shader pair was proven on the target RTV path
from this log. Do not convert thousands of placeholder hashes by count. Start
instead from the exact executed pairs, the three source resources, CBV/SRV
bindings, readback transition, and full BMP at the target Present.

For a 950M continuation, create a separate
`evidence\GTX_950M\runs\<date>\950m-pre-menu-v1.x-...` profile. Verify the
950M GPU, runtime DLL hashes, D3D11 tiled tier, the 950M package, and all
run-local override hashes before launch. Reuse the 1050 Ti diagnostic method
and evidence format, but do not copy the 1050 Ti manifest as a 950M runtime
attestation.

### 950M version and shader policy

Use independent `950m-*` versions; never inherit `1050ti-*` hash attestation
as 950M evidence. Start with a locally verified 950M resource-snapshot profile
and then use this promotion ladder:

1. **Cataloged**: exact hash and stage observed in a 950M run.
2. **Ready**: hash-matched SM5 override loaded in the 950M proxy.
3. **Executed**: a 950M draw or dispatch invoked the shader.
4. **Target-correlated**: the shader/resource pair is linked to the source RTV
   and copy-to-backbuffer path.
5. **Pixel-validated**: full source/backbuffer BMP proves stable non-black
   menu pixels.

Do not convert all 950M placeholder hashes at once. Promote only exact
target-correlated hashes, and keep failures and negative runs in versioned
directories rather than deleting them.

## Latest status — 2026-11-11

- v012's `Load` probe changed from encoded zero at Present #100 to encoded
  ones by #1300. These are diagnostic shader colors, not gameplay.
- v013's `SampleLevel` probe was encoded zero at #100. Its log stops at #800;
  no verified #1300 capture exists.
- v014's no-override baseline is uniformly black at #100; at #400 the
  matched RTV history reports 66 skipped draws. Real Hela rendering is still
  not demonstrated on this GPU.
- **v015 managed run:** Present #100 and #400 source/backbuffer BMPs are
  uniformly black (`0/921,600` non-black pixels). No SRV creation failure or
  `0x8007000e` was logged; 64 target descriptor writes were observed. This
  does not explain the earlier failure or demonstrate scene rendering: the
  final heartbeat has zero successful draws and 947 VS skips. See the
  [v015 run analysis](./runs/20261111/950m-v015-no-shader-override-srv-diagnostics-20261111-20261111-113122270/README.md).
- The manager verified v015's runtime fingerprints and restored the exact
  pre-run v001 DLLs and six installed shader overrides. Its run folder retains
  the log, report, four BMPs, manifest, and restore point. See the
  [checkpoint](./checkpoint-20261111.md) for the next diagnostic step.

## Result

- The first launch used the existing conservative setup. Hela exited with
  code 0 after about 13 seconds without creating the game window.
- The diagnostic run used the packaged proxy, diagnostic tracing, and the
  two corrected vertex-shader overrides. Hela showed Unreal's crash window
  and exited with code 3 after about 10 seconds.
- The proxy selected the GTX 950M, whose D3D11 device reports Tiled Resources
  Tier 1. Hela requested a 2 GiB reserved buffer; the proxy returned
  `E_NOTIMPL` because the reserved-resource path requires D3D11 Tier 2.
- No PSO, swapchain Present, draw, or backbuffer rendering was reached. The
  test does not establish black-screen rendering on this GPU.
- The game's original DLLs, INI, and shader overrides were restored after
  the run. The full crash context and minidump are not included; the summary
  omits machine/login identifiers, and the minidump can contain process
  memory.

## Tier 0 follow-up

- The proxy was forced to report D3D12 Tiled Resources Tier 0 while the
  native GTX 950M D3D11 device remained at Tier 1.
- Hela created a window titled `Hela` and remained alive for about 70
  seconds. The trace has no `CreateReservedResource` requests; the previous
  2 GiB reserved-buffer assertion was avoided.
- Final trace: `present=1025`, `PSO(G=983,C=647)`, `draw(ok)=0`, `bb=0`,
  `skip-other=1801`, `dispatch(ok)=0`, `placed(ok)=80`, `overlap=0`.
- The process exited with `0xC0000005` (access violation); no matching
  Unreal crash context identified the fault. The window and Presents were
  observed, but no backbuffer draw was recorded, so black-screen pixels
  were not independently confirmed.
- Trace review confirms both corrected VS overrides (`20e0826b236597a1`,
  `4ce363172cfd70be`) loaded successfully; `INPUT_LAYOUT_FAILED=0` and
  `PSO GRAPHICS FAILED=0`. The missing-VS hypothesis is false.
- All 1,801 skipped draws have `missing-vs=0`, `missing-shader=1` and
  account for `skip-other=1801`. Their missing PS hashes were
  `44b0a992c2304254` (1,024 draws), `d77600bde0076c33` (484), and
  `4c1bc8bf7e457acd` (293); each was logged as a placeholder, not ready.
- This first Tier 0 run did not include those PS overrides; the follow-up
  below records the targeted test and its result.
- Original DLLs, INI, binding map, and overrides were restored. Details and
  the full trace are in `tier0-test-summary.txt` and
  `d3d12on11-tier0-test.log`.

## Pixel-shader override follow-up

- Two initial launches used the conservative `shader_model=5_0` setting and
  exited before PSO creation; they are not valid rendering-test results.
  The controlled rerun used the exact diagnostic proxy binary and
  temporarily advertised Shader Model 6.7, matching the prior Tier 0 run.
- All three missing PS candidates loaded as SM5 overrides, alongside both
  corrected VS overrides. The trace recorded zero `DRAW_SKIPPED`,
  `INPUT_LAYOUT_FAILED`, and `PSO GRAPHICS FAILED` records.
- Final heartbeat: `present=1857`, `PSO(G=983,C=2158)`,
  `draw(ok)=2505`, `rtv=2505`, `bb=0`, `skip-other=0`,
  `dispatch(ok)=0`, `placed(ok)=179`, `overlap=0`. No
  `CreateReservedResource` request was recorded.
- The `Hela` window was observed for about 77 seconds; the process ended
  with `0xC0000005` after about 114 seconds total. The faulting module is
  unknown. Draws now execute, but none was classified as targeting the
  backbuffer; a visible rendered frame is still unverified.
- The original DLL, INI, and three installed overrides were restored.
  Details and the full trace are in `pixel-shader-test-summary.txt` and
  `d3d12on11-tier0-ps-overrides-test.log`.

## Backbuffer copy trace

- The instrumented DXGI path logged 7,057 successful `GetBuffer` wraps;
  each corresponding resource was flagged as a backbuffer. The native
  swapchain texture is 1536x864, format 24 (`R10G10B10A2_UNORM`).
- The run recorded 3,527 `CopyResource` operations targeting a backbuffer
  and 3,527 successful Presents. The first observed backbuffer copy
  immediately preceded a successful Present. This points to a copy/blit
  composite, not a draw directly to the swapchain buffer.
- `draw(ok)=3903` and `bb=0` are consistent with that route: no draw was
  classified as targeting the backbuffer. The process exited with code
  `16384` after about 135 seconds; its window was observed for about 103
  seconds.
- This confirms the proxy submits copies into the swapchain texture, but
  no pixel readback or screenshot was captured. A visible rendered image
  is therefore still unverified. Next, inspect the copy source and capture
  a bounded backbuffer readback before Present.
- DLLs, INI, and all staged overrides were restored. The raw 59 MB trace
  remains in the local test backup; only the sanitized summary is included
  in this evidence directory.

## Backbuffer readback

- Added opt-in readback before `Present` and `Present1` using
  `D3D12ON11_BACKBUFFER_READBACK=1`, sampled every 100th present. The
  sampler decodes `R10G10B10A2_UNORM` correctly and also supports common
  8-bit RGBA/BGRA formats.
- The GTX 950M run recorded 2,068 Presents and 2,068 `CopyResource`
  operations targeting the backbuffer. Twenty samples were captured, from
  Present #100 through #2000. All five reported points in every sample
  were `(R,G,B,A)=(0,0,0,0)`; all 100-point non-black counts were `0/100`.
- The window title was observed. The process exited with code `16384`
  (`0x4000`) after about 134 seconds. All game DLLs, INI settings, and
  staged overrides were restored and verified against their backups.
- This establishes that the sampled backbuffer pixels were zero at those
  points; it does not prove every pixel in every frame was black. The next
  diagnostic should sample the source texture of the backbuffer copy to
  distinguish empty source content from a copy-path problem. The next run
  should explicitly use windowed mode (`-windowed`).
- The 31 MB raw run log is retained locally at
  `D:\SteamLibrary\steamapps\common\Hela Demo\Hela\Binaries\Win64\D3D12On11Patch\backups\950m-backbuffer-readback-20261110-013458\backbuffer-readback-test.log`;
  see `backbuffer-readback-test-summary.txt` for the sanitized findings.

## Copy-source readback (initial attempt; window mode not verified)

- The run passed `-windowed`, but the UE5 `GameUserSettings.ini` still had
  `FullscreenMode=1` (borderless fullscreen). A visible Hela window title
  did not prove that the window was actually windowed; treat that launch
  mode as unverified.
- Source-resource tracking showed three textures, each 1536x864, format 24
  (`R10G10B10A2_UNORM`), and sampled them alongside the destination before
  Present. The run logged
  2,357 backbuffer-targeted `CopyResource` calls and 23 source/destination
  readback pairs (#100 through #2300). Every source sample had all five
  reported points at `(0,0,0,0)` and `0/100` non-black pixels; every
  destination sample was likewise zero.
- No source-query, staging-creation, Map, or format errors were recorded.
  The process exited naturally with `0x4000` after about 128 seconds.
  Original DLLs, INI, and overrides were restored and verified.
- The source and destination samples confirm zero-valued pixels at the
  sampled locations and times. They do not identify which upstream draw,
  clear, binding, or resource-lifetime issue caused this. Next, trace
  writes/clears to the three source resources and inspect the render-target
  bindings before their copies.
- Sanitized results are in `copy-source-readback-test-summary.txt`. The
  34.9 MB raw log remains local at
  `D:\SteamLibrary\steamapps\common\Hela Demo\Hela\Binaries\Win64\D3D12On11Patch\backups\950m-copy-source-readback-20261110-014625\copy-source-readback-test.log`.

## Verified windowed source readback

- Set UE5 `FullscreenMode`, `LastConfirmedFullscreenMode`, and
  `PreferredFullscreenMode` to `2` in the local user settings and launched
  with `-windowed -ResX=1280 -ResY=720`.
- Verified the actual HWND style was `0x14CF0000` (standard overlapped
  window). Its outer bounds were 1296x759 on a 1536x864 monitor, confirming
  it was not borderless fullscreen. The persistent UE5 config now remains
  in windowed mode at 1280x720.
- The run recorded 2,056 Presents and backbuffer copies, with 20 sampled
  source/destination pairs (#100 through #2000). Source and destination
  were 1280x720, format 24. All five points in all source samples were
  `(0,0,0,0)`, with `0/100` non-black pixels; destination samples were
  also zero.
- Hela exited naturally with `0x4000` after about 92 seconds. Proxy DLLs,
  proxy INI, and shader overrides were restored and verified.
- See `verified-windowed-source-readback-test-summary.txt`; the 31.2 MB
  trace remains local at
  `D:\SteamLibrary\steamapps\common\Hela Demo\Hela\Binaries\Win64\D3D12On11Patch\backups\950m-windowed-source-readback-20261110-015307\windowed-source-readback-test.log`.

## Red backbuffer injection and full readback

- Enabled `D3D12ON11_INJECTION_TEST=1` to clear the swapchain backbuffer to
  red after the proxy flush and immediately before the native Present.
  The run also enabled the sampled and full texture readbacks.
- Launched with `Hela -graphicsadapter=0 -windowed -ResX=1280 -ResY=720`.
  The live HWND style was `0x14CF0000` (`WS_OVERLAPPEDWINDOW`), with outer
  bounds 1296x759; this was a normal window rather than borderless fullscreen.
- The game completed 2,159 Presents and exited naturally with code 16384
  (`0x4000`) after about 97 seconds. Twenty-one source/destination samples
  were captured from Present #100 through #2100. Every source sample was
  black (`0/100` non-black pixels); every injected backbuffer sample was
  red (`100/100`).
- The full 1280x720, format-24 (`R10G10B10A2_UNORM`) BMPs from Present #100
  confirm the complete image contents: all 921,600 source pixels are black,
  and all 921,600 backbuffer pixels are red. The user had already confirmed
  the red window visually in the preceding injection run.
- This confirms the red clear reaches the backbuffer before Present and the
  swapchain can display the injected frame. The CopyResource source is
  already black, so the missing game image is upstream of the final
  backbuffer/Present step; the exact upstream cause remains unproven.
- The proxy DLLs and INI were restored and verified against their original
  SHA-256 hashes. UE5 settings remain windowed at 1280x720.
- `Present100_source.bmp` and `Present100_backbuffer.bmp` contain the full
  readbacks. The 28.6 MB raw trace remains local at
  `D:\SteamLibrary\steamapps\common\Hela Demo\Hela\Binaries\Win64\D3D12On11Patch\backups\950m-full-readback-20261110-022941\full-readback-test.log`.
- Added opt-in `[CMDLIST_SUMMARY]` counters for each submitted command list
  when diagnostic render tracing is enabled. The summary counts draw
  executions, skipped draws, executions with an RTV, backbuffer draws, and
  CopyResource operations to the backbuffer.

## Source-resource draw history with SM5 overrides

- Built and tested the source-resource history instrumentation together with
  the DXGI bridge. All six CTest tests passed with the GTX 950M Tier 0
  diagnostic setting.
- Ran `Hela-Win64-Shipping.exe` directly, without Steam, using
  `-graphicsadapter=0 -windowed -ResX=1280 -ResY=720`,
  `D3D12ON11_SHADER_MODEL=6_7`, diagnostic tracing, and Tier 0. Installed
  the matching Release DLL pair and the SM5 overrides for VS
  `20e0826b236597a1` and PS `44b0a992c2304254` / `d77600bde0076c33`.
- The window remained black. At Present #100, three CopyResource source
  resources reported 64, 64, and 66 operations; all were executed draws,
  with zero skipped draws and zero RTV clears. The first 20 retained
  entries per resource show alternating draws with the listed PS shaders,
  six elements, one instance, RTV slot 0, and viewport 1280x720.
- Final trace: `present=2478`, `draw(ok)=2853`, `skip-vs=204`,
  `skip-other=0`, `placed(ok)=334`, `overlap=6`. Hela exited with `0x4000`
  after about 132.6 seconds.
- This confirms execution of draw commands on the CopyResource source
  resources, not their pixel values. Pixel readback was not enabled in this
  run, so the black window does not prove these particular draws produced
  black pixels. A same-run readback with the overrides is still needed.
- The original game DLLs, INI, and installed overrides were restored and
  verified. The 34.1 MB raw trace remains local; see
  `source-history-test-summary.txt` for the sanitized findings.

## Files

- `d3d12on11-before.log`: baseline launch log from the conservative setup.
- `d3d12on11-950m-test.log`: complete proxy trace from the diagnostic run.
- `crash-summary.txt`: sanitized Unreal assertion summary.
- `d3d12on11-tier0-test.log`: complete trace from the forced Tier 0 run.
- `tier0-test-summary.txt`: setup, results, and interpretation of the Tier 0 run.
- `d3d12on11-tier0-ps-overrides-test.log`: full trace after staging the
  three missing PS overrides.
- `pixel-shader-test-summary.txt`: results and interpretation of the PS test.
- `backbuffer-copy-test-summary.txt`: sanitized GetBuffer, resource-wrap,
  backbuffer-copy, and Present counts from the instrumented run.
- `backbuffer-readback-test-summary.txt`: sampled backbuffer pixel values
  and the bounded GTX 950M readback test result.
- `copy-source-readback-test-summary.txt`: source and destination
  measurements from the initial follow-up run; actual window mode was not
  verified.
- `verified-windowed-source-readback-test-summary.txt`: results from the
  run whose window style and dimensions were explicitly verified.
- `full-red-backbuffer-readback-test-summary.txt`: results from the verified
  windowed red-injection run, including complete source and destination
  BMP analysis.
- `source-history-test-summary.txt`: results from the source-resource
  history run with the three target SM5 overrides loaded.
- `Present100_source.bmp` and `Present100_backbuffer.bmp`: full 1280x720
  BMP readbacks from Present #100.

## November 10 raw run artifacts

- The non-empty `.log`, `.json`, and `.txt` outputs from the November 10
  diagnostic runs are archived under `runs/20261110/`, preserving their
  original backup-directory names. This includes the early Tier 0,
  backbuffer/source readback and injection runs, the live-monitor attempt,
  and the v001/v002/v003 override runs.
- The archive contains 49 files (about 496 MB uncompressed). Copied
  `previous-*` and `pre-run-*` log snapshots and empty stderr files are
  omitted; the primary run logs, monitor output, and JSON reports are kept.
- The v003 parser regression was verified against the archived v003 log:
  Present #100's source history has 66 draws, zero skipped draws, and the
  target VS/PS hashes. Source and backbuffer readbacks remain black, so the
  rendering cause is still undetermined.

## November 11 real-pixel-shader runs

- **v005:** both converted game PS shaders loaded as SM5 overrides with the
  fullscreen VS test override. The t0 texel was white
  (`RGBA=(255,255,255,255)`), but source and backbuffer Present #100 BMPs were
  fully black (`0/921,600` non-black pixels). This rules out a black t0 for
  the measured draws.
- **v006:** the target input layout and first four vertices were read back.
  Their `ATTRIBUTE3` colors are black RGB/alpha 1, and both target PS shaders
  consume that color for RGB. The Present #100 history contains only the
  target VS and those two PS hashes (66 draws, no skips or clears).
- The v006 heartbeat reached 465 Presents with 2,126 compute PSOs created and
  zero compute dispatches executed. The measured quad's output is explained,
  but the missing scene is not. Full setup, interpretation, hashes, raw logs,
  reports, and BMPs are archived under
  [`runs/20261111`](./runs/20261111/).
- v005/v006 restored the game directory to v001 and verified its DLL hashes.
- **v007:** the white-`COLOR` VS override was confirmed loaded with both
  converted PS overrides and white t0, but Present #100 source/backbuffer
  stayed entirely black. Black vertex colors alone are not sufficient to
  explain the readback; see
  [`950m-v007-white-vertex-color-20261111`](./runs/20261111/950m-v007-white-vertex-color-20261111/README.md).
- New v008 tooling now requires a run manifest that pins both proxy DLLs and
  the exact full `.cso` override set before launching. The proxy also logs
  the SHA-256 of loaded modules and shader overrides; the launcher rejects
  missing or mismatched runtime attestations.
- **v008:** with both target PSs returning solid white, the source and
  backbuffer readbacks became entirely white (`921,600/921,600` non-black).
  Runtime DLL and all required shader fingerprints matched the run manifest
  both before and after the test. This proves the controlled draw/copy path
  works and narrows the remaining black output to the converted PS logic or
  its inputs/translation; it is not normal game rendering. Details and all
  hashes are in
  [`950m-v008-solid-ps-fingerprint-20261111`](./runs/20261111/950m-v008-solid-ps-fingerprint-20261111/README.md).
- **v009:** both target PSs returned sampled t0 RGB multiplied by the
  interpolated VS `COLOR`, with gamma and constant-buffer adjustments
  bypassed. The white t0 probe and pipeline statistics confirm the target
  draws execute on the captured source resource (921,600 and 230,400 PS
  invocations). Both Present #100 readbacks nevertheless remained entirely
  black. The actual per-pixel PS sample and interpolated inputs are still
  unknown; the next test will emit sampled t0, `COLOR`, and their product in
  separate channels. Evidence and hashes are archived in
  [`950m-v009-sample-color-fingerprint-20261111`](./runs/20261111/950m-v009-sample-color-fingerprint-20261111/README.md).
