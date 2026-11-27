# Replication evidence (sanitized)

This bundle belongs to the highlighted November 27 run. It preserves the proxy log in gzip form, the path-redacted run/monitor/process manifests, the restore manifest, and a crash-stack summary containing module offsets only.

- `d3d12on11.sanitized.log.gz`: complete proxy log; local machine paths are replaced with placeholders.
- `run-manifest.json`: runtime/override SHA-256 values and the exact launch profile.
- `manager-run-sanitized.json`, `live-monitor-report.json`, `process-status.json`: sanitized run results.
- `restore-manifest.json`: sanitized restoration record.
- `crash-stack.sanitized.txt`: exception and module offsets without process IDs or absolute addresses.
- `replication-summary.json`: compact, machine-readable outcome.

The gzip file can be expanded with Python's `gzip` module. BMP readbacks, the process minidump, shader `.cso` files, and game stdout/stderr are intentionally not published.
