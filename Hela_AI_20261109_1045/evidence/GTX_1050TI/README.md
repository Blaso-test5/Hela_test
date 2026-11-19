# GTX 1050 Ti evidence

The 2026-11-10 proxy investigation, measured results, diagnostic extracts,
lossless readback previews, and selected raw logs are in
[`runs/20261110`](runs/20261110/README.md).

This evidence is separate from [`GTX_950M`](../GTX_950M/README.md).

The November 17 pre-menu crash analysis is documented in
[`crash-analysis-20261117.md`](./crash-analysis-20261117.md). It distinguishes
the NVIDIA exception instruction from Hela's deeper stack frames and records
the failed shader-override attestation.

The isolated November 19 compute-candidate dispatch/readback test is in
[`compute-candidate-dispatch-20261119.md`](./compute-candidate-dispatch-20261119.md).
It verifies one candidate shader through the proxy on the GTX 1050 Ti host;
it is not a GTX 950M or in-game result.

## Current milestone and objectives

The current shader milestone is a hash-matched SM5 override that created a
compute PSO, ran a dispatch through the proxy, and returned all expected
records in a controlled test. This verifies that candidate and test path only;
it does not establish equivalence for all inputs or that Hela uses the shader.
The November 17 pre-menu run also failed shader-override attestation. Real
in-game graphics through the proxy have not yet been verified.

### Versioned pre-menu replay (2026-11-19)

[Run `1050ti-pre-menu-v1.1.0-14override-dump-173223055`](./runs/20261119/1050ti-pre-menu-v1.1.0-14override-dump-173223055)
matched both expected runtime DLL hashes, but it was not an exact replay of
the November 17 profile:
shader dumping was enabled and `D3D12ON11_OVERRIDES` pointed to the installed
override directory instead of the run-local baseline directory. Hela exited
before creating a swapchain or reaching Present; the #3000 capture was
incomplete, heartbeat counters remained `swap=0/present=0`, and no shader
readiness/placeholders or dump files were produced. The package's
`20e0826b236597a1.cso` hash matched its manifest; ProxyManager's failed
attestation reflects the missing runtime `SHADER READY` evidence, not a
package hash mismatch. Installation restoration succeeded.

The November 17 log records shader dumping disabled and a run-local override
directory. Profile
[`1050ti-pre-menu-v1.1.1-14override-no-dump`](./runs/20261119/profiles/1050ti-pre-menu-v1.1.1-14override-no-dump)
was prepared and attested with 14 overrides. The elevated
[v1.1.1 run](./runs/20261119/1050ti-pre-menu-v1.1.1-14override-no-dump-174726437)
used those exact override hashes, the expected runtime DLL hashes, shader
dumping disabled, and run-local overrides. It still was not an exact replay:
the historical run used `Run-HelaCandidateReadback.ps1`, while v1.1.1 used
`ProxyManager` with `Start-HelaLiveMonitor.ps1`.

The logs show a material initialization difference. The historical run logged
`DIAGNOSTIC_MASK_NVIDIA` with exposed vendor `0xffff`, created the NVIDIA
device via `adapter source=proxy-unwrap`, and reported tiled-resource tier 3.
V1.1.1 had no adapter-mask log, used `adapter source=direct`, and reported
tiled-resource tier 0. Its caller tracing was also enabled (historical: off).
The original runner explicitly enables the NVIDIA adapter wrapper; the live
monitor launcher hardcodes tiled-resource tier 0. V1.1.1 stopped after one
startup heartbeat with `swap=0/present=0`; it produced no shader readiness,
readbacks, or completed #1400 capture. Thus the Manager override-attestation
error is a consequence of not reaching shader creation, not a bad `.cso`.
These differences are the leading explanation for the failed reproduction;
they have not yet been isolated as the only cause.

Do not rerun the v1.1.1 script as a baseline. The next replay must use the
historical launch environment, including NVIDIA masking and tiled-resource
tier 3, and verify those markers in the startup log before evaluating graphics.
The installed diagnostic DLLs remain restored to hashes
`17b8fdea…c2466e35` and `0801ea3b…330ae996`.

Profile
[`1050ti-pre-menu-v1.1.2-historical-env`](./runs/20261119/profiles/1050ti-pre-menu-v1.1.2-historical-env)
pins the same 14 override hashes and runtime DLL package as v1.1.1, and adds a
run-scoped child environment: NVIDIA masking (`0xffff`), diagnostic
capabilities, tiled-resource tier 3, caller tracing off, render tracing on,
SM 6.7, no shader dump, and run-local override/dump directories. Its launch
arguments restore the historical stdout/flush flags. ProxyManager records the
profile hash and resolved child environment; the launcher fails closed unless
the log shows the masked `proxy-unwrap` adapter, tier 3, and caller tracing
off. Existing manifests without a launch profile retain their prior behavior.

Run
[`1050ti-pre-menu-v1.1.2-historical-env-181838503`](./runs/20261119/1050ti-pre-menu-v1.1.2-historical-env-181838503)
reached Present #1400 with runtime module hashes verified, required override
attestation, the expected `proxy-unwrap`/`0xffff` adapter, tier 3, and caller
tracing off. The user confirmed the pre-menu was visible. The manager initially
marked the run failed because its tier matcher expected the older
`D3D12_OPTIONS(0)` spelling; the actual log uses `D3D12_OPTIONS`. The matcher
now accepts both, and post-run validation of the preserved log passed. The
original manager result remains unchanged; its failure and the follow-up
validation are both retained.

Important boundary: the full-frame source and backbuffer BMPs at Presents #100
and #1400 are entirely black (0/921600 non-black pixels). The visual
observation and readbacks therefore disagree; the captured pixels do not
independently validate rendered color. See
[`post-run-validation.json`](./runs/20261119/1050ti-pre-menu-v1.1.2-historical-env-181838503/post-run-validation.json).
ProxyManager restored the installed runtime DLLs and no Hela/Crashpad
processes remain. The owned Hela and Crashpad egress rules were removed and
verified absent; this run was online, and no zero-egress claim is made.

The first v1.1.2 attempt
[`1050ti-pre-menu-v1.1.2-historical-env-181738069`](./runs/20261119/1050ti-pre-menu-v1.1.2-historical-env-181738069)
stopped before launching Hela because PowerShell's
`ProcessStartInfo.EnvironmentVariables` exposes `Keys`, not `AllKeys`; the
launcher was corrected before the successful launch attempt.

### Immediate objective: first real Hela graphics

Reach the first visibly rendered in-game Hela frame through the proxy on the
GTX 1050 Ti. Verify the expected shader overrides in the same run and correlate
shader use with draw/dispatch and fresh Present readbacks; a forced-color
diagnostic does not count as game graphics. Resolve the known override
attestation failure before treating a run as valid. A rendered frame will
prove progress on that workload, not complete DX11 compatibility.

### Next objective: Hela shader conversion and verification

After reaching that graphics milestone, extend the existing Shader Lab
conversion workflow rather than assume that compiling every remaining shader
is sufficient. For each shader observed in a defined game run, the pipeline
should record its stage and hashes; convert and compile it to SM5; validate
the expected override match and PSO creation; and, where practical, test
execution against deterministic inputs and compare with the original DXIL.
Unsupported or unverified cases must remain explicit failures, not be
silently accepted.

The end-to-end acceptance gate is a correlated run showing the expected
shader overrides loaded, no unexplained missing-shader placeholders or
skipped work on the target path, successful draw/dispatch and Present
readbacks, and visibly rendered Hela output. The goal is zero unclassified
failures for the measured workload, not a blanket promise that arbitrary SM6
shaders can always be converted without errors.

### Late-draw correlation

The readback Present count and `copy_sequence` are separate counters. First
correlate each `TARGET_DRAW_COPY` record with the target Present's
`READBACK_IDENTITY` by copy sequence and resource identity; do not use
`matches_previous_copy_source` at draw time as proof of the Present source.
If the target draw is identified, rerun with
`Run-HelaCandidateReadback.ps1 -TargetDrawIndex <draw_index>` to bypass the
normal diagnostic hit cap and recapture vertex data and `t0` for that selected
indexed draw. A zero index disables the targeted capture. This instrumentation
has compiled and passed the 8 native tests; no new Hela run has validated it.
