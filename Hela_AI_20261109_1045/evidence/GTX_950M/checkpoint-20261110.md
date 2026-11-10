# Hela GTX 950M checkpoint — 2026-11-10

## Goal and status

Continue the controlled, reversible investigation until Hela renders a
visible game image on the GTX 950M. The goal is **not yet reached**.
Use BridgeCode/Argon and other available AI models to analyze the evidence
and prepare targeted changes; do not ask them to regenerate whole files.
Preserve each diagnostic DLL/shader set as a version and keep original
backups untouched. Push meaningful new tools and sanitized run evidence.

## Current machine state

- Hela is not running.
- The game directory contains diagnostic v001 DLLs:
  - `d3d12.dll`: SHA-256
    `209C78CA0FA244228A967A6A98465CD625A92D1696FE6D1A68EDC45CFDEAF062`
  - `dxgi.dll`: SHA-256
    `06F0836565BFC577956BB4CB2540E104C94DE1FB68AC6FBA5283D8082DFDC2E2`
- `D3D12On11Patch\d3d12on11.ini` enables logging, shader dumps, SM 6.7,
  diagnostic capabilities, caller tracing, and diagnostic render tracing.
- The normal `D3D12On11Patch\overrides` directory contains the pre-v003
  overrides. The isolated v003 magenta/fullscreen overrides are under
  `D3D12On11Patch\versions\v003-force-fullscreen-magenta\overrides`.
- Do not replace DLLs while Hela is running. Keep new binaries and shader
  overrides in a new version directory rather than overwriting v001/v003.

## Verified findings

- Run v003 used the v001 proxy DLLs and the isolated v003 shader overrides.
  Logs record `SHADER READY` for VS `20e0826b236597a1` and PS
  `44b0a992c2304254` / `d77600bde0076c33`.
- The v003 VS forces fullscreen positions; both target PS overrides emit
  constant magenta. At Present #100, source history records 66 draws,
  zero skipped draws, and zero clears for the readback-matched source.
- The source and backbuffer BMPs each contain 0 non-black pixels out of
  921,600. This run did not produce visible rendering.
- The Live Frame Monitor previously failed to parse native `vs_hash`,
  `ps_hash`, `rtv_slot`, and tuple viewport fields. The parser and regression
  test were fixed; the suite passes 10/10. Re-parsing the real v003 log now
  finds the target shader hashes, RTV slot 0, and 1280x720 viewport.
- The v003 evidence does **not** yet prove that the target draw wrote to the
  resource sampled before Present, nor that the D3D11 fixed-function state
  accepted fragments. Treat theories about missing index buffers, culling,
  scissor, depth, blending, or RTV/SRV hazards as hypotheses until measured.

## Logs and repository

- The non-empty November 10 `.log`, `.json`, and `.txt` run outputs are
  archived under `runs/20261110/` (49 files, approximately 496 MB).
  Duplicate `pre-run`/`previous` snapshots and empty stderr files were not
  included. See `README.md` in this directory for the archive description.
- Live Frame Monitor is included at
  `../../proxy-source/tools/LiveFrameMonitor/` relative to this directory.
- Commits `197e1e2` (monitor) and `6009245` (November 10 evidence) were
  pushed to `Blaso-test5/Hela_test` on `main`.

## Next recommended experiment — not yet performed

Ask Argon to prepare a small, target-hash-gated diagnostic patch in
`src/commands.cpp`; review it before building or running. Keep v001 intact
and produce a separately versioned diagnostic DLL build. Reuse the isolated
v003 magenta/fullscreen overrides so the shader inputs do not change.

For at most the first four target `DrawIndexedInstanced` calls, snapshot the
native D3D11 immediate-context state immediately before the draw:

- Bound index buffer pointer/format/offset, vertex-buffer presence, and
  primitive topology.
- RTV0 and its underlying resource, DSV, and viewport.
- Rasterizer cull/scissor settings and the active scissor rectangle.
- Depth/stencil enable flags, blend enable/write mask, and sample mask.

Log these as bounded `[TARGET_DRAW_DIAG]` records and retain the same-run
Present #100 source/backbuffer readbacks. Start with **read-only state
inspection**; do not clear or modify targets in the first run, because that
would perturb the rendering being diagnosed. Compare the RTV resource with
the actual CopyResource source recorded for Present #100. Interpret the
state as evidence that narrows the next test, not as proof by itself that
fragments reached memory. If the bindings/state look valid, have Argon assess
a subsequent bounded GPU occlusion or pipeline-statistics query.

## Resume checklist

1. Read this checkpoint and the appended November 10 sections in `README.md`.
2. Verify the game is closed and confirm the installed DLL hashes before
   any run.
3. Ask Argon for the targeted patch only (no full-file rewrite), then review
   the exact diff and build/test a new version.
4. Run once with the v003 overrides and bounded target-state logging; save
   the raw log, monitor JSON, and Present #100 captures in a new timestamped
   run directory.
5. Analyze the exact draw/readback correlation before choosing the next
   intervention. Commit and push meaningful plugin/evidence updates.
