# Qwen analysis bundle

This directory is a source-and-evidence bundle for analysis only. It does not contain a proposed patch.

## Pinned inputs

- Hela proxy repository: `Blaso-test5/Hela_test`, commit `4450b7d5b7da77dea2139c68897c37e2d0ab7f9f`
- Shader Lab repository: `Blaso89/Shader-Lab`, commit `29d8ad893344ae477bf555f12ed41d74e048d625`

Start with `manifest.json`. It links the source-file manifest, complete repository tree inventories, and the sanitized evidence index. Those indexes list the individual part paths, line/record ranges, byte counts, and SHA-256 checksums.

Large or previously truncated sources are delivered as contiguous, numbered line slices. Reassemble each source in order and verify its published SHA-256 and line count before analysis; entries with redactions also retain the original commit hash and identify the replaced machine-local values. Content-identical files share one published copy.

The evidence is parsed and allowlisted: no raw logs/stdout, network observations or payloads, Sentry data, shader bytes/HLSL, BMP images, or DLL/CSO binaries. Runtime paths are replaced with symbolic run/game roots; memory addresses are consistently pseudonymized while preserving cross-record identity.

The machine-local `.vscode/mcp.json` configuration is not part of the pinned Shader Lab commit and is omitted. Generated artifacts, build outputs, dependencies, and captured runtime files are not included.

No diagnosis or rendering fix is asserted by this bundle. The prior run established a black source/backbuffer at the capture point; it did not identify the root cause.
