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
