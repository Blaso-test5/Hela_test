# Hela proxy and shader test package

This repository records the Hela D3D12-to-D3D11 investigation. The native
proxy and Shader Lab converter are separate projects, even though they are
stored in the same local workspace.

## Workspace layout

- Hela_test checkout: this repository
- Hela shader export and active proxy working copy: separate local workspaces
- Versioned, portable proxy source: `Hela_AI_20261109_1045\proxy-source`

The versioned proxy source can refresh an existing `d3d12on11` working copy.
Refreshing it adds or replaces maintained project files but does not delete
local build outputs, backups, or other extra files.

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
$Target = 'C:\Path\To\d3d12on11'
robocopy $Source $Target /E /R:2 /W:1
```

`proxy-source` contains the CMake file, maintained C++ sources, generated
headers, tests, and tools. It intentionally excludes machine-specific
`build*` outputs and backups. Build provenance and tested DLL hashes are in
`Hela_AI_20261109_1045\evidence\build-provenance.txt`.

### Shader-use telemetry

With diagnostic render tracing enabled, the proxy reports bounded graphics
draw records for VS/PS/GS/HS/DS and compute dispatch records for CS. The
ledger distinguishes executed from skipped calls and includes missing-stage
masks. It retains up to 512 groups and prints at most 64 per heartbeat.

The proxy also retains the last 64 shader-use record events, including skipped
draws and dispatches, independently of the ledger's group cap. It dumps this
bounded ring at each enabled readback checkpoint (every 100 readback Present
calls), alongside the current native backbuffer identity and last recorded
copy source/sequence. Event ordinals count ledger record events; `execution_count`
may represent multiple commands, so neither is a frame ID or necessarily one
individual indirect draw.

The ring records `present_calls_returned` as observed when each event is
recorded. The separate `readback_present_count` is incremented before the
corresponding Present call; it is not the same counter. These are correlation
watermarks, not frame IDs. `executed` means the proxy emitted the D3D11 call;
`rtv_is_backbuffer` only describes whether a backbuffer RTV was bound. Neither
proves GPU completion or visible contribution to a frame. Correlate the ring
with resources, Present readbacks, and visual validation from the same run.

### Placed-resource alias handling

The GTX 1050 Ti shader-dump run on November 16 reached 329 successful Presents
and then UE reported `E_INVALIDARG` at `D3D12Resources.cpp:1042`. Immediately
before the crash, the proxy log recorded overlapping `CreatePlacedResource`
requests and returned `E_INVALIDARG`; this is a strong candidate for the
failure, although the available crash report does not identify one exact
request.

The proxy now accepts overlapping placements and tracks each resource
independently, avoiding collisions when resources share an offset. This is a
compatibility mitigation, not full D3D12 heap aliasing: each resource still
has a separate D3D11 backing object, and aliasing/UAV resource barriers remain
unimplemented. The placed-resource smoke test covers a large resource
overlapped by a smaller one; an in-game run and visual validation are still
required.

### Monitored game runs

Start the passive TCP monitor in a separate PowerShell window before
launching Hela, using a fresh run directory each time:

```powershell
$Run = Join-Path (Get-Location) 'Hela_AI_20261109_1045\evidence\GTX_1050TI\runs\<run-id>'
$Game = 'C:\Path\To\Hela'
New-Item -ItemType Directory -Force $Run | Out-Null
powershell -NoProfile -File .\Hela_AI_20261109_1045\proxy-source\tools\Watch-HelaTcp.ps1 `
  -GameDirectory $Game `
  -OutputPath (Join-Path $Run 'network-observations.jsonl') `
  -DurationSeconds 1200
```

The JSONL file is appended and flushed per observation, so a later report
failure does not erase the TCP records. It contains connection endpoints and
states for Hela and its Crashpad handler, not packet contents; it does not
cover UDP or prove that no data was sent. The November 16 shader-dump run did
not persist its TCP observations, so its egress status is unknown.

### Refresh the Shader Lab converter fix

The TypeScript Shader Lab is separate from the native proxy. The entry-point
fix and its regression tests are included
as source snapshots under
`Hela_AI_20261109_1045\evidence\converter-source`. To apply them to an
existing Shader Lab checkout:

```powershell
$Repo = (Get-Location).Path
$Plugin = 'C:\Path\To\sm6-to-sm5-mcp'
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

### Sentry egress audit and local block (2026-11-16)

An audit of 30 archived Hela stdout logs from November 14–16 found 60 Sentry
envelope requests, all followed by `HTTP/1.1 200 OK`. Logged request bodies
were 341–353 bytes and had no `x-sentry-error` response header. The requests
went to Sentry's European ingestion service; the project-specific hostname
and `sentry_key` are intentionally omitted. A 200 response confirms endpoint
acceptance, but the archived logs do not preserve every request body.

The locally retained envelope for the November 16 run is a `session` item.
Its final on-disk state says `status=crashed` and `errors=1`, with session
timing and release/environment attributes. That file was updated after the
last logged 200 response, so the exact final state is not proven to have been
accepted. The run also produced a 3.1 MB process minidump after that response.
Its metadata describes process memory, threads, modules, handles, and memory
regions; it is not a full-system image. The dump remains local, and the
