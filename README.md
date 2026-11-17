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
available request logs do not prove that it was uploaded. Neither raw stdout
nor any minidump or Sentry key is included in this repository.

For this Windows user, Sentry auto-initialization was configured off in
`%LOCALAPPDATA%\Hela\Saved\Config\Windows\Engine.ini` with:

```ini
[/Script/Sentry.SentrySettings]
InitAutomatically=False
Dsn=
```

Persistent Windows Firewall outbound block rules were added for the game
and Crashpad uploader. They were configured for all network destinations
and profiles for these exact executables, so if effective they can also
disable other network features. They are machine-local and are not applied
by cloning this repo. The commands used were:

```powershell
$GameDirectory = 'C:\Path\To\Hela'
New-NetFirewallRule -PolicyStore PersistentStore -DisplayName "Hela project - block game outbound" -Direction Outbound -Program (Join-Path $GameDirectory 'Binaries\Win64\Hela-Win64-Shipping.exe') -Action Block -Profile Any -Enabled True

New-NetFirewallRule -PolicyStore PersistentStore -DisplayName "Hela project - block Crashpad outbound" -Direction Outbound -Program (Join-Path $GameDirectory 'Plugins\Sentry\Binaries\Win64\crashpad_handler.exe') -Action Block -Profile Any -Enabled True
```

The rules read back as enabled, outbound, `Block`, and stored persistently
for Domain, Private, and Public profiles. However, a 90-second post-rule
launch observed a Hela-owned TCP connection to an IPv6 endpoint on port 443
(`ESTABLISHED`, then `CLOSE_WAIT`). The endpoint and payload are not included
in this repository. The connection alone cannot establish what data, if any,
was sent. No Crashpad handler process was observed, so this is not evidence
that the minidump was uploaded. No packet capture was running; no Hela log or
Sentry report was modified during the test, and no Windows Firewall log was
present. The payload therefore cannot be reconstructed from available local
artifacts. Hela was closed after the connection was noticed.

An earlier `ActiveStore` lookup found neither named rule, and at that time the
effective policy did not permit the local rules to take effect. A later check
on November 16 at 13:39 CET found no matching rule in either `PersistentStore`
or `ActiveStore`; all three firewall profiles were enabled and allowed local
rules, but the checking shell was not elevated. Thus no effective block was
verified at that later check either.

At 13:45 CET, the user ran both `-Apply` and a read-only verification from the
elevated PowerShell session; both invocations reported effective blocks for
the game and Crashpad executable paths. A separate non-elevated automation
shell still could not enumerate those rules. Do not launch Hela from that
separate shell; run the graphics test only in a context where the fail-closed
verification below succeeds.

`Hela_AI_20261109_1045\proxy-source\tools\Block-HelaEgress.ps1` is a
fail-closed helper. Run it from an elevated PowerShell to
create persistent outbound block rules for the exact game and Crashpad
executables, then verify that both appear in `ActiveStore` with all profiles,
addresses, ports, and protocols covered:

```powershell
$GameDirectory = 'C:\Path\To\Hela'
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\Hela_AI_20261109_1045\proxy-source\tools\Block-HelaEgress.ps1 `
  -GameDirectory $GameDirectory -Apply
```

Without `-Apply`, the helper only verifies existing effective rules. It
refuses to launch Hela unless both blocks are effective; if local rules are
suppressed by policy, the command fails without starting the game. The rules
block all outbound network features of those executables, not only Sentry.
This verifies firewall configuration, not packet-level behavior or other
executables outside these two paths. The per-run TCP monitor above is
best-effort and does not prove that no data was sent.

`Hela_AI_20261109_1045\proxy-source\tools\Block-HelaEgressDefensive.ps1`
provides separate `Apply`, `Verify`, and `Remove` modes for rules it owns.
`Apply` uses resolved executable paths and a dedicated rule group; `Remove`
deletes only rules carrying that group's marker and leaves the older
`Hela project - block ...` rules untouched. All modes require an elevated
PowerShell session and do not launch Hela or generate network traffic:

```powershell
$GameDirectory = 'C:\Path\To\Hela'
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\Hela_AI_20261109_1045\proxy-source\tools\Block-HelaEgressDefensive.ps1 `
  -GameDirectory $GameDirectory -Mode Apply
```

The tool reports `configVerified` separately from `enforcementProven`, which
remains `false`: ActiveStore confirms policy configuration, not that Windows
or another filtering provider enforces it for a future connection. It does
not set machine-wide outbound policy or disable network adapters. If zero
egress is required while enforcement remains uncertain, manually disconnect
network-capable interfaces for the run.

The controlled readback runner records the helper's ActiveStore verification
context separately from its TCP observations. Its `firewallCheck` result is
`observed-nonloopback-established`, `configuration-unverified`,
`monitor-incomplete`, or `no-egress-observed`; it never treats a successful
rule-presence check alone as proof that no connection occurred. The detailed
result contains counts of deduplicated TCP state events and timestamps, not
remote IP addresses. A missing observation is not proof of zero egress, and an
Established TCP state does not prove that application payload was accepted by
a remote service. The pure summary/verdict logic can be checked without
launching Hela:

```powershell
powershell -NoProfile -File `
  .\Hela_AI_20261109_1045\proxy-source\tools\tests\HelaRunEvidence.Tests.ps1
```

For the staged GTX 1050 Ti shader candidates, set `$GameDirectory`,
`$RunDirectory`, and `$ExternalReadbackDirectory` to the local paths, then run
the controlled readback test from the same elevated PowerShell context where
the block verification succeeds:

```powershell
$GameDirectory = 'C:\Path\To\Hela'
$RunDirectory = Join-Path (Get-Location) 'Hela_AI_20261109_1045\evidence\GTX_1050TI\runs\<fresh-run-id>'
$ExternalReadbackDirectory = 'D:\'
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\Hela_AI_20261109_1045\proxy-source\tools\Run-HelaCandidateReadback.ps1 `
  -GameDirectory $GameDirectory `
  -RunDirectory $RunDirectory `
  -ExternalReadbackDirectory $ExternalReadbackDirectory
```

The runner rechecks the effective firewall rules before launch, uses the
12 base run-local overrides plus any explicitly requested candidates and
captures the full source/backbuffer at the requested Present. It snapshots
and restores the existing proxy log and external readback files; sanitizes
Sentry credentials in stdout/stderr; and does not replace the installed proxy
DLLs or alter shared overrides. It refuses to start if the firewall check,
GPU, DLL fingerprints, or staged override checks fail. The experiment's
run-specific override inputs are local and are not published.
Uncaught PowerShell failures are rethrown and also saved in a
`powershell-error-<UTC timestamp>.json` file in the run directory, including
the failing stage, exception, and source position.
Use `-PreflightOnly` to run the same read-only checks and report their results
without changing readbacks/logs or starting Hela.

For a bounded follow-up observation after the target readback, the runner
accepts `-ObserveAfterReadbackSeconds 1..120` (default `0`, preserving the
immediate-close behavior). This only records whether the process remained
alive during that interval; no reliable menu log marker has been established,
so `postReadbackObservation.menuReached` remains `undetermined`. Confirming
the menu requires separate visual observation and must not be inferred from
Present activity or process lifetime.

The first attempt on 2026-11-16 failed before producing a verified readback:
the runner tried to read the proxy log while Hela still had it open. The
revised runner waits for both target BMPs to have stable, nonzero sizes, closes
Hela, and only then reads the run log. It also preserves the run log, readbacks,
and sanitized stdout/stderr before restoring the original external files. Keep
the failed attempt intact and always supply a fresh `RunDirectory`.

The retry run (`compute-sm5-candidate-readback-retry-20261116`) reached 1,508
successful Presents and saved both source and backbuffer at Present 1,400.
However, all four run-local BMPs (Present 100 and 1,400, source and backbuffer)
are exactly black: 1280x720, 0 of 921,600 non-black pixels, with identical
SHA-256 `D43EC90910E666B315661BE75DFEA0AF9722FF471AA30593999567DFFC53AC9C`.
The proxy reported all four candidate compute shaders `SHADER READY`; its final
trace counted 22 dispatches, 1,996 draws to non-backbuffer render targets, no
direct draws to the backbuffer, and 1,508 copies to the backbuffer. Thus the
readback path works, but this run did not produce visible game graphics and
does not establish that the compute candidates fixed rendering. Raw run logs
and captures remain local and are intentionally not published.

The shader-use ledger in that log narrows the next graphics experiment: all 304
skipped draw commands use VS `4ce363172cfd70be` and PS `4c1bc8bf7e457acd`,
with missing-stage mask `0x03`, between Presents 2 and 490; no skipped draw
group appears afterward, and all 18 ledger windows report zero omissions or
overflow. Present 100 falls inside this interval; Present 1,400 is after it.
Their existing SM5 overrides were not among that run's 12 staged files. The
converted VS passes through position and UV; the PS
samples `t0` and applies a color transform, so the pair may be a composition
pass but is not yet identified as scene rendering. A separate 14-override
run is staged locally; it adds only those two SM5 overrides to the previous
run's set and has not been executed.

The more direct black-frame lead is the draw family observed executed after
the skipped-pair window and at the final Present: VS `20e0826b236597a1` +
PS `44b0a992c2304254`. Its first-hit diagnostics show a full-viewport quad
with 921,600 pixel-shader invocations. `t0` is an application-created 1x1
texture, and a GPU readback returned white; the sampled color is multiplied
by vertex `COLOR`, whose sampled `ATTRIBUTE3` bytes are black and opaque
(`00 00 00 FF`). The observed PS constants and alpha formula yield alpha 1,
and blending is enabled with source-alpha / inverse-source-alpha, so the
observed draw fully writes black over its destination. This is the strongest
current explanation for an all-black frame, not evidence that the proxy
substituted a white fallback. However, vertex and texture values were sampled
at startup, not at #1400, and have not yet been confirmed as the live values
at that Present.

The isolated test for the separate startup pair has not been run. The prior run recorded an established IPv6
connection despite the firewall check passing, so do not launch another game
run until outbound blocking is verified effective. Once that gate is resolved,
and the local prepared run directory and overrides are available, run the
isolated candidate set with:

```powershell
$GameDirectory = 'C:\Path\To\Hela'
$RunDirectory = Join-Path (Get-Location) 'Hela_AI_20261109_1045\evidence\GTX_1050TI\runs\20261116\startup-pass-sm5-pair-readback-20261116'
$ExternalReadbackDirectory = 'D:\'
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\Hela_AI_20261109_1045\proxy-source\tools\Run-HelaCandidateReadback.ps1 `
  -GameDirectory $GameDirectory `
  -RunDirectory $RunDirectory `
  -ExternalReadbackDirectory $ExternalReadbackDirectory `
  -AdditionalCandidateHashes '4ce363172cfd70be','4c1bc8bf7e457acd'
```

The same run's firewall gate reported success, but the TCP monitor observed
an established Hela-owned IPv6 connection to port 443. The monitor records
connection metadata only, not payload; it cannot establish whether application
data was sent or what it contained. This observed connection means the
firewall verification must not be treated as proof that egress was blocked.
Preserve the rules and investigate enforcement before relying on them for
another run. Raw network observations remain local.
