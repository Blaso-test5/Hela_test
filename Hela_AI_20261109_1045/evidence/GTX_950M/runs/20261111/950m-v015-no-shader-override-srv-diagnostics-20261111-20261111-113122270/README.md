# GTX 950M v015 managed run — Present #400

Run ID: `950m-v015-no-shader-override-srv-diagnostics-20261111-20261111-113122270`

## Result

- The manager validated the v015 package against the manifest, ran Hela through
  Present #400, verified both runtime module fingerprints, and restored the
  pre-run installation. The game process is no longer running.
- `d3d12.dll` and `dxgi.dll` matched the v001 files after restoration. The six
  pre-run shader overrides were restored and hash-checked from the saved
  restore point.
- The source and backbuffer BMPs at Presents #100 and #400 are each
  1280x720, with `0/921,600` non-black pixels. The sampled source/backbuffer
  readbacks at #100 and #400 are also black.
- The run recorded 64 `TARGET_DESCRIPTOR_WRITE` events, but no
  `CreateShaderResourceView failed`, `E_OUTOFMEMORY`, `0x8007000e`, or
  `MODULE_FINGERPRINT_FAILED` markers. This failure did not reproduce in this
  run; that alone does not establish why it appeared in earlier versions.
- No shader override was active: the log recorded 4,957 placeholder events,
  zero ready events, and the monitor retained 256 unique placeholders (its
  configured cap). The final heartbeat reached Present #401 with zero
  successful draws, 947 VS skips, and zero successful dispatches. Rendering
  of the original Hela scene is not demonstrated.

## Evidence

- [run-manifest.json](./run-manifest.json) pins the tested DLL hashes.
- [live-monitor-report.json](./live-monitor-report.json) records the verified
  runtime identity, readbacks, BMP inspection, and shader status.
- [d3d12on11.log](./d3d12on11.log) is the run log.
- [`readbacks/`](./readbacks/) contains the four full-frame captures.
- [`restore-point/`](./restore-point/) contains the exact pre-run DLL and
  override snapshot and its hash manifest.
- [manager-run.json](./manager-run.json) records the managed run and restore
  status.

The manager kept the run's outputs in this unique directory and restored the
original game log/BMP paths after the run. The installed v001 DLLs and pre-run
override set remain active.
