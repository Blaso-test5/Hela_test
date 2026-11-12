# Hela proxy and shader test package

This repository records the Hela D3D12-to-D3D11 investigation. The native
proxy and Shader Lab converter are separate projects, even though they are
stored in the same local workspace.

## Local workspace mapping on this PC

- Hela_test checkout: `D:\Plugin_ChatGpt\GHIDRA MCP\Hela-Export\Repositories\Hela_test`
- Hela shader export: `D:\Plugin_ChatGpt\GHIDRA MCP\Hela-Export`
- Active native proxy source: `D:\Plugin_ChatGpt\GHIDRA MCP\Plugin Nuovi\sm6-to-sm5-mcp\d3d12on11`
- Versioned, portable proxy source: `Hela_AI_20261109_1045\proxy-source` in this checkout

The versioned proxy source is the update source for the active `d3d12on11`
folder. Refreshing it adds or replaces maintained project files but does not
delete local build outputs, backups, or other extra files.

## Get the latest files on another Windows PC

For a new checkout:

```powershell
git clone --branch main https://github.com/Blaso-test5/Hela_test.git
Set-Location .\Hela_test
```

For an existing checkout:

```powershell
git pull --ff-only origin main
```

### Rebuild the native D3D12-to-D3D11 proxy

The complete portable CMake project is
`Hela_AI_20261109_1045\proxy-source`. Build and run its native tests directly
from the clone:

```powershell
$Source = Join-Path (Get-Location) 'Hela_AI_20261109_1045\proxy-source'
$Build = Join-Path (Get-Location) 'build-proxy'
cmake -S $Source -B $Build -A x64
cmake --build $Build --config Release
ctest --test-dir $Build -C Release --output-on-failure
```

For a diagnostic build with proxy PDB symbols available to crash reports,
add `-DD3D12ON11_DIAGNOSTIC_SYMBOLS=ON` to the CMake configure command.

To refresh an existing local `d3d12on11` source folder instead, copy the
versioned project files over it (this does not delete local files):

```powershell
$Source = Join-Path (Get-Location) 'Hela_AI_20261109_1045\proxy-source'
$Target = 'D:\Plugin_ChatGpt\GHIDRA MCP\Plugin Nuovi\sm6-to-sm5-mcp\d3d12on11'
robocopy $Source $Target /E /R:2 /W:1
```

`proxy-source` contains the CMake file, maintained C++ sources, generated
headers, tests, and tools. It intentionally excludes machine-specific
`build*` outputs and backups. Build provenance and tested DLL hashes are in
`Hela_AI_20261109_1045\evidence\build-provenance.txt`.

### Refresh the Shader Lab converter fix

The TypeScript Shader Lab at
`D:\Plugin_ChatGpt\GHIDRA MCP\Plugin Nuovi\sm6-to-sm5-mcp` is separate from
the native proxy. The entry-point fix and its regression tests are included
as source snapshots under
`Hela_AI_20261109_1045\evidence\converter-source`. To apply them to an
existing Shader Lab checkout:

```powershell
$Repo = (Get-Location).Path
$Plugin = 'D:\Plugin_ChatGpt\GHIDRA MCP\Plugin Nuovi\sm6-to-sm5-mcp'
Copy-Item (Join-Path $Repo 'Hela_AI_20261109_1045\evidence\converter-source\hlsl-post.ts') `
  (Join-Path $Plugin 'src\hlsl-post.ts') -Force
Copy-Item (Join-Path $Repo 'Hela_AI_20261109_1045\evidence\converter-source\test.ts') `
  (Join-Path $Plugin 'src\test.ts') -Force
Push-Location $Plugin
npm run build
npm test
Pop-Location
```

The game-run reports, full logs, shader inputs/overrides, and package
checksums are in `Hela_AI_20261109_1045`.
The original-versus-candidate VS signature diagnosis is in
`Hela_AI_20261109_1045\evidence\input-signature-analysis-20261109.txt`.

### Capture a proxy crash report

The optional vectored handler is installed after normal proxy
initialization, not from `DllMain`. Enable it only for a diagnostic run:

```powershell
$env:D3D12ON11_CRASH_DIAGNOSTICS = '1'
```

It records first-chance access violations, writes a `.txt` report and a
minidump beside `d3d12.dll` (or to the process temp directory if that
location cannot be resolved), then continues normal exception dispatch.
An access violation may be handled by the process, so a report alone does
not prove the game crashed; correlate it with the process exit and UE log.
Use a diagnostic build and make its matching `d3d12.pdb` available to WinDbg
to resolve proxy frames to function names. The handler itself records module
offsets. The handler is off by default.
