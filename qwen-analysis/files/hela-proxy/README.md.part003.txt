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
