# Hela proxy and shader test package

This repository records the Hela D3D12-to-D3D11 investigation. The native
proxy and Shader Lab converter are separate projects, even though they are
stored in the same local workspace.

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

To refresh an existing local `d3d12on11` source folder instead, copy the
versioned project files over it (this does not delete local files):

```powershell
$Source = Join-Path (Get-Location) 'Hela_AI_20261109_1045\proxy-source'
$Target = 'D:\Plugin_ChatGPT\GHIDRA MCP\Plugin Nuovi\sm6-to-sm5-mcp\d3d12on11'
robocopy $Source $Target /E /R:2 /W:1
```

`proxy-source` contains the CMake file, maintained C++ sources, generated
headers, tests, and tools. It intentionally excludes machine-specific
`build*` outputs and backups. Build provenance and tested DLL hashes are in
`Hela_AI_20261109_1045\evidence\build-provenance.txt`.

### Refresh the Shader Lab converter fix

The TypeScript Shader Lab at
`D:\Plugin_ChatGPT\GHIDRA MCP\Plugin Nuovi\sm6-to-sm5-mcp` is separate from
the native proxy. The entry-point fix and its regression tests are included
as source snapshots under
`Hela_AI_20261109_1045\evidence\converter-source`. To apply them to an
existing Shader Lab checkout:

```powershell
$Repo = (Get-Location).Path
$Plugin = 'D:\Plugin_ChatGPT\GHIDRA MCP\Plugin Nuovi\sm6-to-sm5-mcp'
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
