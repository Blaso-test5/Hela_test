#Requires -Version 5.1
<#
    Self-contained tests for Get-HelaProxySnapshot.ps1 (no Pester, no external dependencies).
    Run:  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Tests\Get-HelaProxySnapshot.Tests.ps1
    Exit code = number of failed tests. All fixtures live in a temporary directory that is removed at the end.
#>
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$ScriptPath = [System.IO.Path]::GetFullPath([System.IO.Path]::Combine($PSScriptRoot, '..', 'Get-HelaProxySnapshot.ps1'))
if (-not [System.IO.File]::Exists($ScriptPath)) { throw "Script under test not found: $ScriptPath" }

$TempRoot = [System.IO.Path]::Combine([System.IO.Path]::GetTempPath(), 'hela-snapshot-tests-' + [Guid]::NewGuid().ToString('N'))
[void][System.IO.Directory]::CreateDirectory($TempRoot)
$Utf8 = New-Object System.Text.UTF8Encoding($false)
$Ov = 'shader_overrides'
$script:Asserts = 0
$script:Failures = 0
$script:Passed = 0

# ---------------------------------------------------------------- helpers

function Join-P { param([string]$A, [string]$B) return [System.IO.Path]::Combine($A, $B) }

function Get-Bytes { param([string]$Text) return , $Utf8.GetBytes($Text) }

function Get-TextSha {
    param([string]$Text, [switch]$Upper)
    $s = [System.Security.Cryptography.SHA256]::Create()
    try { $h = (($s.ComputeHash((Get-Bytes $Text)) | ForEach-Object { $_.ToString('x2') }) -join '') } finally { $s.Dispose() }
    if ($Upper) { return $h.ToUpperInvariant() }
    return $h
}

function New-Sandbox {
    $p = Join-P $TempRoot ([Guid]::NewGuid().ToString('N'))
    [void][System.IO.Directory]::CreateDirectory($p)
    return $p
}

function New-Install {
    param([string]$Dir, [hashtable]$Files)
    [void][System.IO.Directory]::CreateDirectory($Dir)
    foreach ($k in $Files.Keys) {
        $f = Join-P $Dir $k
        [void][System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($f))
        [System.IO.File]::WriteAllBytes($f, (Get-Bytes $Files[$k]))
    }
}

function Get-Tree {
    param([string]$Dir)
    $lines = New-Object System.Collections.Generic.List[string]
    foreach ($e in [System.IO.Directory]::GetFileSystemEntries($Dir, '*', [System.IO.SearchOption]::AllDirectories)) {
        if ([System.IO.Directory]::Exists($e)) {
            $lines.Add("D|$e")
        } else {
            $fi = New-Object System.IO.FileInfo($e)
            $h = Get-TextSha ([System.IO.File]::ReadAllText($e))
            $lines.Add("F|$e|$($fi.Length)|$($fi.LastWriteTimeUtc.Ticks)|$h")
        }
    }
    $lines.Sort()
    return ($lines -join "`n")
}

function Write-Json {
    param([string]$Path, $Obj)
    [System.IO.File]::WriteAllText($Path, ($Obj | ConvertTo-Json -Depth 8), $Utf8)
}

function New-RunManifest {
    param([string]$Path, [string]$D3d12, [string]$Dxgi, $Overrides = @{}, $Required = @(),
          [string]$Schema = 'hela-run-manifest/v1', [string]$Attestation = 'module-and-shader-sha256')
    Write-Json $Path ([ordered]@{
        schema             = $Schema
        testVersion        = 'unit-test'
        runtimeAttestation = $Attestation
        runtime            = [ordered]@{ d3d12 = $D3d12; dxgi = $Dxgi }
        overrides          = $Overrides
        requiredOverrides  = $Required
    })
}

function Invoke-Snapshot {
    param([hashtable]$Params)
    $ErrorActionPreference = 'Continue'
    $global:LASTEXITCODE = $null
    $threw = $null
    $out = @()
    try { $out = @(& $ScriptPath @Params 6>$null 2>&1) } catch { $threw = $_.Exception.Message }
    $code = $global:LASTEXITCODE
    $errs = @($out | Where-Object { $_ -is [System.Management.Automation.ErrorRecord] })
    $plain = @($out | Where-Object { $_ -isnot [System.Management.Automation.ErrorRecord] })
    $text = (($plain | ForEach-Object { [string]$_ }) -join "`n")
    $json = $null
    if ($text.Trim().Length -gt 0) { $json = $text | ConvertFrom-Json }
    return [pscustomobject]@{ ExitCode = $code; Json = $json; Errors = $errs; Threw = $threw; Text = $text }
}

function Assert-True {
    param($Cond, [string]$What)
    if (-not $Cond) { throw "Assertion failed: $What" }
    $script:Asserts++
}

function Assert-Equal {
    param($Actual, $Expected, [string]$What)
    if ($Actual -ne $Expected) { throw "$What : expected '$Expected' but got '$Actual'" }
    $script:Asserts++
}

function Find-Finding {
    param($Res, [string]$Cat, [string]$Name)
    $hit = @($Res.Json.items | Where-Object { $_.category -eq $Cat -and $_.name -eq $Name })
    if ($hit.Count -ne 1) { throw "Expected exactly one finding $Cat/$Name but found $($hit.Count)" }
    return $hit[0]
}

function Assert-Status {
    param($Res, [string]$Cat, [string]$Name, [string]$Status)
    $f = Find-Finding $Res $Cat $Name
    Assert-Equal $f.status $Status "status of $Cat/$Name"
}

function Assert-Rejected {
    param($Res, [string]$What)
    Assert-Equal $Res.ExitCode 2 "$What exit code"
    Assert-True ($null -eq $Res.Json) "$What must not print a JSON report"
    Assert-True ($Res.Errors.Count -ge 1) "$What must emit an error"
}

function Test-Case {
    param([string]$Name, [scriptblock]$Body)
    try {
        & $Body
        $script:Passed++
        Write-Host "PASS  $Name" -ForegroundColor Green
    } catch {
        $script:Failures++
        Write-Host "FAIL  $Name" -ForegroundColor Red
        Write-Host "      $($_.Exception.Message)" -ForegroundColor Red
    }
}

# Literal legacy restore manifest taken from the project (runtime as strings, no overrides_directory_present).
$LegacyExample = @'
{
  "schema": "hela-proxy-restore/v1",
  "game_directory": "D:\\SteamLibrary\\steamapps\\common\\Hela Demo\\Hela\\Binaries\\Win64",
  "created_utc": "2026-11-11T11:31:22.5341877Z",
  "runtime": {
    "d3d12.dll": "209c78ca0fa244228a967a6a98465cd625a92d1696fe6d1a68edc45cfdeaf062",
    "dxgi.dll": "06f0836565bfc577956bb4cb2540e104c94de1fb68ac6fba5283d8082dfdc2e2"
  },
  "overrides": [
    {"path":"20e0826b236597a1.cso","bytes":1768,"sha256":"5d844557b3a5f78c108aca027b14432f3eba52791057234f7310c487926a3d3e"},
    {"path":"25e657206d761172.cso","bytes":372,"sha256":"84e92d1475080ac8919f855a2eb6f06e8c0829dad0fe76c5e453b6d1101e99ad"},
    {"path":"38acdb88bb8801b9.cso","bytes":58476,"sha256":"0724669de377f5bcd07ccb96f3b3172e955d1fa7c56c99e09c997601d12df9cc"},
    {"path":"44b0a992c2304254.cso","bytes":1560,"sha256":"43c029b121212d0a37e0a00aaf983d9b6f7ea3ee593412c46ef22c52513d263d"},
    {"path":"74867736557b00e8.cso","bytes":1540,"sha256":"fd8dbefb9aba6699fa8bfed527a6388ed65682b8e9ce28cd529f1f7112fc3e40"},
    {"path":"d77600bde0076c33.cso","bytes":1560,"sha256":"9f6fee68b9c8c824f106426db501012ebc176e35a22c7a8cd47e3e94719207b4"}
  ]
}
'@

# The real no-override run manifest from the project.
$RealNoOverrideRun = @'
{
  "schema": "hela-run-manifest/v1",
  "testVersion": "950m-v015-no-shader-override-srv-diagnostics-20261111",
  "runtimeAttestation": "module-and-shader-sha256",
  "runtime": {
    "d3d12": "0B986B326848CDE6148F86E7F7BA8D17795293115F51F17DEC2593758484A605",
    "dxgi": "252B4C8E62CC176A3B5568C53F76730E4CFB9432998DF556EDD502D73C5F70CD"
  },
  "overrides": {},
  "requiredOverrides": []
}
'@

function New-ModernRestore {
    param([string]$Path, [string]$Game, $Runtime, [bool]$DirPresent, $Overrides = @())
    Write-Json $Path ([ordered]@{
        schema                      = 'hela-proxy-restore/v1'
        game_directory              = $Game
        created_utc                 = '2026-11-11T11:31:22.5341877Z'
        runtime                     = $Runtime
        overrides_directory_present = $DirPresent
        overrides                   = $Overrides
    })
}

# ---------------------------------------------------------------- tests

try {

Test-Case 'run no-override: matching install is a Match (uppercase manifest hashes, empty overrides dir)' {
    $sb = New-Sandbox; $game = Join-P $sb 'game'
    New-Install $game @{ 'd3d12.dll' = 'D3D12-PAYLOAD'; 'dxgi.dll' = 'DXGI-PAYLOAD' }
    [void][System.IO.Directory]::CreateDirectory((Join-P $game $Ov))
    $m = Join-P $sb 'run.json'
    New-RunManifest $m (Get-TextSha 'D3D12-PAYLOAD' -Upper) (Get-TextSha 'DXGI-PAYLOAD' -Upper)
    $before = Get-Tree $game
    $r = Invoke-Snapshot @{ GameDirectory = $game; RunManifestPath = $m; OverridesDirectoryName = $Ov }
    Assert-Equal $r.ExitCode 0 'exit code'
    Assert-Equal $r.Json.summary.verdict 'Match' 'verdict'
    Assert-Status $r 'runtime' 'd3d12.dll' 'Match'
    Assert-Status $r 'runtime' 'dxgi.dll' 'Match'
    Assert-Equal $r.Json.summary.counts.Match 2 'match count'
    Assert-Equal $r.Json.reference.format 'run-manifest' 'format'
    Assert-Equal (Get-Tree $game) $before 'install tree unchanged (read-only)'
}

Test-Case 'run no-override: absent overrides dir is also a Match (empty set == empty set)' {
    $sb = New-Sandbox; $game = Join-P $sb 'game'
    New-Install $game @{ 'd3d12.dll' = 'A'; 'dxgi.dll' = 'B' }
    $m = Join-P $sb 'run.json'
    New-RunManifest $m (Get-TextSha 'A' -Upper) (Get-TextSha 'B' -Upper)
    $r = Invoke-Snapshot @{ GameDirectory = $game; RunManifestPath = $m; OverridesDirectoryName = $Ov }
    Assert-Equal $r.ExitCode 0 'exit code'
    Assert-Equal $r.Json.summary.verdict 'Match' 'verdict'
}

Test-Case 'run: without -OverridesDirectoryName the result is Inconclusive, never a false Match' {
    $sb = New-Sandbox; $game = Join-P $sb 'game'
    New-Install $game @{ 'd3d12.dll' = 'A'; 'dxgi.dll' = 'B' }
    $m = Join-P $sb 'run.json'
    New-RunManifest $m (Get-TextSha 'A' -Upper) (Get-TextSha 'B' -Upper)
    $r = Invoke-Snapshot @{ GameDirectory = $game; RunManifestPath = $m }
    Assert-Equal $r.ExitCode 3 'exit code'
    Assert-Equal $r.Json.summary.verdict 'Inconclusive' 'verdict'
    Assert-Status $r 'overrides' '(set)' 'NotAttestable'
}

Test-Case 'run: the real no-override example manifest is accepted (mismatch against a fake install)' {
    $sb = New-Sandbox; $game = Join-P $sb 'game'
    New-Install $game @{ 'd3d12.dll' = 'not the real dll'; 'dxgi.dll' = 'not the real dll either' }
    $m = Join-P $sb 'real-run.json'
    [System.IO.File]::WriteAllText($m, $RealNoOverrideRun, $Utf8)
    $r = Invoke-Snapshot @{ GameDirectory = $game; RunManifestPath = $m; OverridesDirectoryName = $Ov }
    Assert-Equal $r.ExitCode 1 'exit code'
    Assert-Equal $r.Json.reference.testVersion '950m-v015-no-shader-override-srv-diagnostics-20261111' 'testVersion'
    Assert-Status $r 'runtime' 'd3d12.dll' 'Mismatch'
    Assert-Status $r 'runtime' 'dxgi.dll' 'Mismatch'
    Assert-Equal (Find-Finding $r 'runtime' 'd3d12.dll').expected.sha256 '0b986b326848cde6148f86e7f7ba8d17795293115f51f17dec2593758484a605' 'expected hash is normalised'
}

Test-Case 'run: DLL hash mismatch and DLL absent' {
    $sb = New-Sandbox; $game = Join-P $sb 'game'
    New-Install $game @{ 'd3d12.dll' = 'TAMPERED' }
    $m = Join-P $sb 'run.json'
    New-RunManifest $m (Get-TextSha 'D3D12-ORIGINAL' -Upper) (Get-TextSha 'DXGI-ORIGINAL' -Upper)
    $r = Invoke-Snapshot @{ GameDirectory = $game; RunManifestPath = $m; OverridesDirectoryName = $Ov }
    Assert-Equal $r.ExitCode 1 'exit code'
    Assert-Equal $r.Json.summary.verdict 'Mismatch' 'verdict'
    Assert-Status $r 'runtime' 'd3d12.dll' 'Mismatch'
    Assert-Status $r 'runtime' 'dxgi.dll' 'Missing'
    Assert-Equal (Find-Finding $r 'runtime' 'd3d12.dll').actual.sha256 (Get-TextSha 'TAMPERED') 'live hash reported'
    Assert-Equal (Find-Finding $r 'runtime' 'dxgi.dll').actual.kind 'absent' 'absent kind'
}

Test-Case 'run: override missing / extra / hash mismatch / match (required flag reported), install untouched' {
    $sb = New-Sandbox; $game = Join-P $sb 'game'
    New-Install $game @{
        'd3d12.dll' = 'A'; 'dxgi.dll' = 'B'
        "$Ov\a.cso" = 'AAA'; "$Ov\b.cso" = 'b-changed'; "$Ov\d.cso" = 'DDD'; "$Ov\ignore.txt" = 'not a cso'
    }
    $m = Join-P $sb 'run.json'
    New-RunManifest $m (Get-TextSha 'A' -Upper) (Get-TextSha 'B' -Upper) `
        @{ 'a.cso' = (Get-TextSha 'AAA' -Upper); 'b.cso' = (Get-TextSha 'b-original' -Upper); 'c.cso' = (Get-TextSha 'CCC' -Upper) } `
        @('a.cso', 'c.cso')
    $before = Get-Tree $game
    $r = Invoke-Snapshot @{ GameDirectory = $game; RunManifestPath = $m; OverridesDirectoryName = $Ov }
    Assert-Equal $r.ExitCode 1 'exit code'
    Assert-Status $r 'override' 'a.cso' 'Match'
    Assert-Status $r 'override' 'b.cso' 'Mismatch'
    Assert-Status $r 'override' 'c.cso' 'Missing'
    Assert-Status $r 'override' 'd.cso' 'Extra'
    Assert-Equal (Find-Finding $r 'override' 'c.cso').required $true 'c.cso required'
    Assert-Equal (Find-Finding $r 'override' 'b.cso').required $false 'b.cso not required'
    Assert-Equal (Find-Finding $r 'override' 'd.cso').actual.sha256 (Get-TextSha 'DDD') 'extra file is hashed'
    Assert-True (@($r.Json.items | Where-Object { $_.name -eq 'ignore.txt' }).Count -eq 0) 'non-.cso files are ignored'
    Assert-Equal (Get-Tree $game) $before 'install tree unchanged (read-only)'
}

Test-Case 'run: overrides dir absent while the manifest lists overrides -> Missing' {
    $sb = New-Sandbox; $game = Join-P $sb 'game'
    New-Install $game @{ 'd3d12.dll' = 'A'; 'dxgi.dll' = 'B' }
    $m = Join-P $sb 'run.json'
    New-RunManifest $m (Get-TextSha 'A') (Get-TextSha 'B') @{ 'a.cso' = (Get-TextSha 'AAA') } @('a.cso')
    $r = Invoke-Snapshot @{ GameDirectory = $game; RunManifestPath = $m; OverridesDirectoryName = $Ov }
    Assert-Equal $r.ExitCode 1 'exit code'
    Assert-Status $r 'override' 'a.cso' 'Missing'
}

Test-Case 'input validation: bad manifests and bad paths are errors (exit 2, no JSON), never successes' {
    $sb = New-Sandbox; $game = Join-P $sb 'game'
    New-Install $game @{ 'd3d12.dll' = 'A'; 'dxgi.dll' = 'B' }
    $h = Get-TextSha 'x'
    $cases = @(
        @{ Name = 'traversal key';       Ovr = @{ '..\evil.cso' = $h } }
        @{ Name = 'nested traversal';    Ovr = @{ 'sub\..\..\evil.cso' = $h } }
        @{ Name = 'absolute key';        Ovr = @{ 'C:\evil.cso' = $h } }
        @{ Name = 'rooted key';          Ovr = @{ '\evil.cso' = $h } }
        @{ Name = 'non-cso key';         Ovr = @{ 'a.txt' = $h } }
        @{ Name = 'bad override hash';   Ovr = @{ 'a.cso' = 'xyz' } }
        @{ Name = 'required not listed'; Ovr = @{ 'a.cso' = $h }; Req = @('b.cso') }
        @{ Name = 'wrong schema';        Ovr = @{}; Schema = 'hela-run-manifest/v2' }
        @{ Name = 'wrong attestation';   Ovr = @{}; Att = 'module-only' }
    )
    foreach ($c in $cases) {
        $m = Join-P $sb 'bad.json'
        if (Test-Path -LiteralPath $m) { Remove-Item -LiteralPath $m -Force }
        $p = @{ Path = $m; D3d12 = $h; Dxgi = $h; Overrides = $c.Ovr }
        if ($c.ContainsKey('Req')) { $p.Required = $c.Req }
        if ($c.ContainsKey('Schema')) { $p.Schema = $c.Schema }
        if ($c.ContainsKey('Att')) { $p.Attestation = $c.Att }
        New-RunManifest @p
        $r = Invoke-Snapshot @{ GameDirectory = $game; RunManifestPath = $m; OverridesDirectoryName = $Ov }
        Assert-Rejected $r $c.Name
    }
    # malformed JSON, missing manifest, missing game dir, bad overrides dir name
    $bad = Join-P $sb 'malformed.json'
    [System.IO.File]::WriteAllText($bad, '{ not json', $Utf8)
    Assert-Rejected (Invoke-Snapshot @{ GameDirectory = $game; RunManifestPath = $bad }) 'malformed json'
    Assert-Rejected (Invoke-Snapshot @{ GameDirectory = $game; RunManifestPath = (Join-P $sb 'nope.json') }) 'missing manifest'
    $ok = Join-P $sb 'ok.json'
    New-RunManifest $ok $h $h
    Assert-Rejected (Invoke-Snapshot @{ GameDirectory = (Join-P $sb 'no-game'); RunManifestPath = $ok }) 'missing game dir'
    Assert-Rejected (Invoke-Snapshot @{ GameDirectory = $game; RunManifestPath = $ok; OverridesDirectoryName = '..\x' }) 'traversal overrides dir name'
    Assert-Rejected (Invoke-Snapshot @{ GameDirectory = $game; RunManifestPath = $ok; OverridesDirectoryName = 'C:\x' }) 'absolute overrides dir name'
}

Test-Case 'parameter sets: -RunManifestPath and -RestorePointPath together are refused' {
    $sb = New-Sandbox; $game = Join-P $sb 'game'
    New-Install $game @{ 'd3d12.dll' = 'A' }
    $r = Invoke-Snapshot @{ GameDirectory = $game; RunManifestPath = (Join-P $sb 'a.json'); RestorePointPath = (Join-P $sb 'b.json') }
    Assert-True ($null -ne $r.Threw) 'binding error expected'
    Assert-True ($null -eq $r.Json) 'no report'
}

Test-Case 'restore modern: DLLs and overrides directory originally absent -> Match when still absent' {
    $sb = New-Sandbox; $game = Join-P $sb 'game'
    [void][System.IO.Directory]::CreateDirectory($game)
    $m = Join-P $sb 'restore.json'
    New-ModernRestore $m $game ([ordered]@{
        'd3d12.dll' = [ordered]@{ present = $false; sha256 = $null }
        'dxgi.dll'  = [ordered]@{ present = $false; sha256 = $null }
    }) $false
    $r = Invoke-Snapshot @{ GameDirectory = $game; RestorePointPath = $m; OverridesDirectoryName = $Ov }
    Assert-Equal $r.ExitCode 0 'exit code'
    Assert-Equal $r.Json.summary.verdict 'Match' 'verdict'
    Assert-Equal $r.Json.reference.format 'restore-modern' 'format must be modern, not legacy'
    $d = Find-Finding $r 'runtime' 'd3d12.dll'
    Assert-Equal $d.status 'Match' 'd3d12 status'
    Assert-Equal $d.expected.present $false 'd3d12 expected.present'
    $dir = Find-Finding $r 'overridesDirectory' $Ov
    Assert-Equal $dir.status 'Match' 'overrides dir status'
    Assert-Equal $dir.expected.present $false 'overrides dir expected.present'
}

Test-Case 'restore modern: things that should be absent but exist are Extra' {
    $sb = New-Sandbox; $game = Join-P $sb 'game'
    New-Install $game @{ 'd3d12.dll' = 'proxy left behind'; "$Ov\x.cso" = 'leftover' }
    $m = Join-P $sb 'restore.json'
    New-ModernRestore $m $game ([ordered]@{
        'd3d12.dll' = [ordered]@{ present = $false; sha256 = $null }
        'dxgi.dll'  = [ordered]@{ present = $false; sha256 = $null }
    }) $false
    $before = Get-Tree $game
    $r = Invoke-Snapshot @{ GameDirectory = $game; RestorePointPath = $m; OverridesDirectoryName = $Ov }
    Assert-Equal $r.ExitCode 1 'exit code'
    Assert-Status $r 'runtime' 'd3d12.dll' 'Extra'
    Assert-Status $r 'runtime' 'dxgi.dll' 'Match'
    Assert-Status $r 'overridesDirectory' $Ov 'Extra'
    Assert-Status $r 'override' 'x.cso' 'Extra'
    Assert-Equal (Get-Tree $game) $before 'install tree unchanged (read-only)'
}

Test-Case 'restore modern: present with hashes matches; present without hash is NotAttestable (Inconclusive)' {
    $sb = New-Sandbox; $game = Join-P $sb 'game'
    New-Install $game @{ 'd3d12.dll' = 'ORIG-D3D12'; 'dxgi.dll' = 'ORIG-DXGI'; "$Ov\a.cso" = 'AAA' }
    $m = Join-P $sb 'restore.json'
    New-ModernRestore $m $game ([ordered]@{
        'd3d12.dll' = [ordered]@{ present = $true; sha256 = (Get-TextSha 'ORIG-D3D12') }
        'dxgi.dll'  = [ordered]@{ present = $true; sha256 = $null }
    }) $true @(, [ordered]@{ path = 'a.cso'; bytes = 3; sha256 = (Get-TextSha 'AAA') })
    $r = Invoke-Snapshot @{ GameDirectory = $game; RestorePointPath = $m; OverridesDirectoryName = $Ov }
    Assert-Equal $r.ExitCode 3 'exit code'
    Assert-Equal $r.Json.summary.verdict 'Inconclusive' 'verdict'
    Assert-Status $r 'runtime' 'd3d12.dll' 'Match'
    Assert-Status $r 'runtime' 'dxgi.dll' 'NotAttestable'
    Assert-Status $r 'overridesDirectory' $Ov 'Match'
    Assert-Status $r 'override' 'a.cso' 'Match'
}

Test-Case 'restore modern: size mismatch with equal hash string is still reported' {
    $sb = New-Sandbox; $game = Join-P $sb 'game'
    New-Install $game @{ 'd3d12.dll' = 'A'; 'dxgi.dll' = 'B'; "$Ov\a.cso" = 'AAA' }
    $m = Join-P $sb 'restore.json'
    New-ModernRestore $m $game ([ordered]@{
        'd3d12.dll' = [ordered]@{ present = $true; sha256 = (Get-TextSha 'A') }
        'dxgi.dll'  = [ordered]@{ present = $true; sha256 = (Get-TextSha 'B') }
    }) $true @(, [ordered]@{ path = 'a.cso'; bytes = 999; sha256 = (Get-TextSha 'AAA') })
    $r = Invoke-Snapshot @{ GameDirectory = $game; RestorePointPath = $m; OverridesDirectoryName = $Ov }
    Assert-Equal $r.ExitCode 1 'exit code'
    Assert-Status $r 'override' 'a.cso' 'Mismatch'
}

Test-Case 'restore modern: inconsistent manifest (dir absent but overrides listed) is rejected' {
    $sb = New-Sandbox; $game = Join-P $sb 'game'
    [void][System.IO.Directory]::CreateDirectory($game)
    $m = Join-P $sb 'restore.json'
    New-ModernRestore $m $game ([ordered]@{
        'd3d12.dll' = [ordered]@{ present = $false; sha256 = $null }
        'dxgi.dll'  = [ordered]@{ present = $false; sha256 = $null }
    }) $false @(, [ordered]@{ path = 'a.cso'; bytes = 3; sha256 = (Get-TextSha 'AAA') })
    Assert-Rejected (Invoke-Snapshot @{ GameDirectory = $game; RestorePointPath = $m; OverridesDirectoryName = $Ov }) 'inconsistent restore manifest'
}

Test-Case 'restore legacy (real example): recognised as legacy; no claim of original absence' {
    $sb = New-Sandbox; $game = Join-P $sb 'game'
    [void][System.IO.Directory]::CreateDirectory($game)
    $m = Join-P $sb 'legacy.json'
    [System.IO.File]::WriteAllText($m, $LegacyExample, $Utf8)
    $r = Invoke-Snapshot @{ GameDirectory = $game; RestorePointPath = $m; OverridesDirectoryName = $Ov }
    Assert-Equal $r.ExitCode 1 'exit code'
    Assert-Equal $r.Json.reference.format 'restore-legacy' 'format'
    $d = Find-Finding $r 'runtime' 'd3d12.dll'
    Assert-Equal $d.status 'Missing' 'd3d12 status'
    Assert-Equal $d.expected.present $true 'legacy string means present'
    Assert-Equal $d.expected.sha256 '209c78ca0fa244228a967a6a98465cd625a92d1696fe6d1a68edc45cfdeaf062' 'legacy hash'
    $dir = Find-Finding $r 'overridesDirectory' $Ov
    Assert-Equal $dir.expected.present $true 'legacy semantics assume the directory present'
    Assert-Equal $dir.status 'Missing' 'overrides dir status'
    Assert-True ($dir.reason -match 'legacy') 'reason mentions the legacy assumption'
    $ovr = @($r.Json.items | Where-Object { $_.category -eq 'override' })
    Assert-Equal $ovr.Count 6 'six recorded overrides'
    foreach ($o in $ovr) { Assert-Equal $o.status 'Missing' "status of $($o.name)" }
    $absentClaims = @($r.Json.items | Where-Object { $null -ne $_.expected -and $_.expected.present -eq $false })
    Assert-Equal $absentClaims.Count 0 'no item may claim "originally absent" from legacy data'
    Assert-True ($r.Json.notes.Count -ge 1) 'legacy notes emitted'
}

Test-Case 'restore legacy: matching install is a Match, and format is not confused with modern' {
    $sb = New-Sandbox; $game = Join-P $sb 'game'
    New-Install $game @{ 'd3d12.dll' = 'L-D3D12'; 'dxgi.dll' = 'L-DXGI'; "$Ov\a.cso" = 'AAA'; "$Ov\b.cso" = 'BBBB' }
    $m = Join-P $sb 'legacy.json'
    Write-Json $m ([ordered]@{
        schema         = 'hela-proxy-restore/v1'
        game_directory = $game
        created_utc    = '2026-11-11T11:31:22.5341877Z'
        runtime        = [ordered]@{ 'd3d12.dll' = (Get-TextSha 'L-D3D12'); 'dxgi.dll' = (Get-TextSha 'L-DXGI') }
        overrides      = @(
            [ordered]@{ path = 'a.cso'; bytes = 3; sha256 = (Get-TextSha 'AAA') },
            [ordered]@{ path = 'b.cso'; bytes = 4; sha256 = (Get-TextSha 'BBBB') }
        )
    })
    $r = Invoke-Snapshot @{ GameDirectory = $game; RestorePointPath = $m; OverridesDirectoryName = $Ov }
    Assert-Equal $r.ExitCode 0 'exit code'
    Assert-Equal $r.Json.summary.verdict 'Match' 'verdict'
    Assert-Equal $r.Json.reference.format 'restore-legacy' 'format'
    Assert-Equal $r.Json.summary.counts.Match 5 'two runtime + overrides dir + two overrides'
}

Test-Case 'restore point as directory + game_directory fallback; restore point stays untouched' {
    $sb = New-Sandbox; $game = Join-P $sb 'game'; $rp = Join-P $sb 'restore-point'
    [void][System.IO.Directory]::CreateDirectory($game)
    New-Install $rp @{ 'backup\d3d12.dll' = 'whatever' }
    New-ModernRestore (Join-P $rp 'restore-manifest.json') $game ([ordered]@{
        'd3d12.dll' = [ordered]@{ present = $false; sha256 = $null }
        'dxgi.dll'  = [ordered]@{ present = $false; sha256 = $null }
    }) $false
    $beforeRp = Get-Tree $rp; $beforeGame = Get-Tree $game
    $r = Invoke-Snapshot @{ RestorePointPath = $rp; OverridesDirectoryName = $Ov }
    Assert-Equal $r.ExitCode 0 'exit code'
    Assert-Equal $r.Json.gameDirectory.source 'restore-manifest' 'game directory source'
    Assert-Equal (Get-Tree $rp) $beforeRp 'restore point unchanged'
    Assert-Equal (Get-Tree $game) $beforeGame 'game directory unchanged'
}

Test-Case 'inventory mode: hashes only, InventoryOnly verdict' {
    $sb = New-Sandbox; $game = Join-P $sb 'game'
    New-Install $game @{ 'd3d12.dll' = 'A'; "$Ov\a.cso" = 'AAA' }
    $r = Invoke-Snapshot @{ GameDirectory = $game; OverridesDirectoryName = $Ov }
    Assert-Equal $r.ExitCode 0 'exit code'
    Assert-Equal $r.Json.summary.verdict 'InventoryOnly' 'verdict'
    Assert-Status $r 'runtime' 'd3d12.dll' 'Observed'
    Assert-Equal (Find-Finding $r 'runtime' 'd3d12.dll').actual.sha256 (Get-TextSha 'A') 'hash'
    Assert-Equal (Find-Finding $r 'runtime' 'dxgi.dll').actual.kind 'absent' 'dxgi absent'
    Assert-Equal (Find-Finding $r 'override' 'a.cso').actual.sha256 (Get-TextSha 'AAA') 'cso hash'
    $r2 = Invoke-Snapshot @{ GameDirectory = $game }
    Assert-Status $r2 'overrides' '(set)' 'NotInspected'
}

Test-Case '-JsonOut: written outside the install, valid JSON, install untouched' {
    $sb = New-Sandbox; $game = Join-P $sb 'game'; $out = Join-P $sb 'reports'
    New-Install $game @{ 'd3d12.dll' = 'A'; 'dxgi.dll' = 'B' }
    [void][System.IO.Directory]::CreateDirectory($out)
    $m = Join-P $sb 'run.json'
    New-RunManifest $m (Get-TextSha 'A') (Get-TextSha 'B')
    $target = Join-P $out 'snap.json'
    $before = Get-Tree $game
    $r = Invoke-Snapshot @{ GameDirectory = $game; RunManifestPath = $m; OverridesDirectoryName = $Ov; JsonOut = $target }
    Assert-Equal $r.ExitCode 0 'exit code'
    Assert-True ([System.IO.File]::Exists($target)) 'report file exists'
    $disk = [System.IO.File]::ReadAllText($target) | ConvertFrom-Json
    Assert-Equal $disk.schema 'hela-proxy-snapshot/v1' 'report schema'
    Assert-Equal $disk.summary.verdict 'Match' 'report verdict'
    Assert-Equal (Get-Tree $game) $before 'install tree unchanged'
}

Test-Case '-JsonOut: refused inside the install, inside the restore point, over existing files, or with missing parent' {
    $sb = New-Sandbox; $game = Join-P $sb 'game'; $rp = Join-P $sb 'rp'; $out = Join-P $sb 'reports'
    New-Install $game @{ 'd3d12.dll' = 'A'; 'dxgi.dll' = 'B' }
    [void][System.IO.Directory]::CreateDirectory($out)
    [void][System.IO.Directory]::CreateDirectory($rp)
    New-ModernRestore (Join-P $rp 'restore-manifest.json') $game ([ordered]@{
        'd3d12.dll' = [ordered]@{ present = $true; sha256 = (Get-TextSha 'A') }
        'dxgi.dll'  = [ordered]@{ present = $true; sha256 = (Get-TextSha 'B') }
    }) $false
    $m = Join-P $sb 'run.json'
    New-RunManifest $m (Get-TextSha 'A') (Get-TextSha 'B')
    $beforeGame = Get-Tree $game; $beforeRp = Get-Tree $rp

    $inGame = Join-P $game 'snap.json'
    Assert-Rejected (Invoke-Snapshot @{ GameDirectory = $game; RunManifestPath = $m; JsonOut = $inGame }) 'JsonOut inside game dir'
    Assert-True (-not [System.IO.File]::Exists($inGame)) 'nothing created inside the game dir'

    $inSub = Join-P (Join-P $game $Ov) 'snap.json'
    Assert-Rejected (Invoke-Snapshot @{ GameDirectory = $game; RunManifestPath = $m; JsonOut = $inSub }) 'JsonOut inside game subdir'

    $inRp = Join-P $rp 'snap.json'
    Assert-Rejected (Invoke-Snapshot @{ GameDirectory = $game; RestorePointPath = $rp; JsonOut = $inRp }) 'JsonOut inside restore point'
    Assert-True (-not [System.IO.File]::Exists($inRp)) 'nothing created inside the restore point'

    Assert-Rejected (Invoke-Snapshot @{ GameDirectory = $game; RunManifestPath = $m; JsonOut = $m }) 'JsonOut equal to the manifest'
    Assert-Rejected (Invoke-Snapshot @{ GameDirectory = $game; RunManifestPath = $m; JsonOut = (Join-P (Join-P $sb 'nope') 'x.json') }) 'JsonOut with missing parent'
    Assert-Rejected (Invoke-Snapshot @{ GameDirectory = $game; RunManifestPath = $m; JsonOut = ($out + '\') }) 'JsonOut that is a directory path'

    $existing = Join-P $out 'existing.json'
    [System.IO.File]::WriteAllText($existing, 'keep me', $Utf8)
    Assert-Rejected (Invoke-Snapshot @{ GameDirectory = $game; RunManifestPath = $m; JsonOut = $existing }) 'JsonOut over existing file'
    Assert-Equal ([System.IO.File]::ReadAllText($existing)) 'keep me' 'existing file untouched'

    Assert-Equal (Get-Tree $game) $beforeGame 'game dir unchanged'
    Assert-Equal (Get-Tree $rp) $beforeRp 'restore point unchanged'
}

} finally {
    try { Remove-Item -LiteralPath $TempRoot -Recurse -Force -ErrorAction SilentlyContinue } catch { }
}

Write-Host ''
Write-Host ("Results: {0} passed, {1} failed, {2} assertions" -f $script:Passed, $script:Failures, $script:Asserts)
if ($script:Failures -gt 0) { exit $script:Failures }
exit 0
