[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [switch]$AllowOnlineRun,
    [string]$PackagePath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $AllowOnlineRun) {
    throw 'This replay can contact external services. Re-run with -AllowOnlineRun only when an online Hela run is explicitly authorized.'
}

$Version = '1050ti-pre-menu-v1.1.2-historical-env'
$ProfileRoot = $PSScriptRoot
$RepoRoot = 'D:\Plugin_ChatGpt\GHIDRA MCP\Hela-Export\Repositories\Hela_test'
$RunRoot = [IO.Path]::GetFullPath((Join-Path $ProfileRoot '..\..\..'))
$GameRoot = 'D:\SteamLibrary\steamapps\common\Hela Demo\Hela'
$GameExe = Join-Path $GameRoot 'Binaries\Win64\Hela-Win64-Shipping.exe'
$BinaryRoot = Split-Path -Parent $GameExe
$PackageRoot = if ([string]::IsNullOrWhiteSpace($PackagePath)) {
    [IO.Path]::GetFullPath((Join-Path $ProfileRoot '..\1050ti-pre-menu-v1.1.1-14override-no-dump\package'))
} else {
    [IO.Path]::GetFullPath($PackagePath)
}
$ManifestPath = Join-Path $ProfileRoot 'run-manifest.json'
$ProxyManager = Join-Path $RepoRoot 'Hela_AI_20261109_1045\proxy-source\tools\LiveFrameMonitor\ProxyManager.ps1'
$TcpWatcher = Join-Path $RepoRoot 'Hela_AI_20261109_1045\proxy-source\tools\Watch-HelaTcp.ps1'
$TargetPresent = 1400
$MonitorTimeoutSeconds = 120
$ExpectedRuntime = @{
    d3d12 = '17b8fdea0485e1c0bd5f1f1332e607051d3a1204a19b369812a0bba2c2466e35'
    dxgi  = '0801ea3b1a28b345a8166aa98a0abd3b7a10d01504d0ebaf2d254a9f330ae996'
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Open PowerShell with Run as administrator, then run this script.'
}

foreach ($requiredPath in @($GameExe, $ProxyManager, $TcpWatcher, $PackageRoot, $ManifestPath)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required run input is missing: $requiredPath"
    }
}

$manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
if ($manifest.testVersion -cne $Version) {
    throw "Manifest version mismatch: $($manifest.testVersion)"
}

$currentRuntime = @{
    d3d12 = (Get-FileHash -LiteralPath (Join-Path $BinaryRoot 'd3d12.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
    dxgi  = (Get-FileHash -LiteralPath (Join-Path $BinaryRoot 'dxgi.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
}
foreach ($role in $ExpectedRuntime.Keys) {
    if ($currentRuntime[$role] -cne $ExpectedRuntime[$role]) {
        throw "Installed $role.dll differs from the expected pre-run restore state."
    }
}

$activeGame = @(Get-CimInstance Win32_Process -Filter "Name='Hela-Win64-Shipping.exe' OR Name='crashpad_handler.exe'" |
    Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith($GameRoot, [StringComparison]::OrdinalIgnoreCase) })
if ($activeGame.Count -gt 0) {
    throw 'Hela or Crashpad is already running; close it before starting this replay.'
}

$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
$sessionRoot = Join-Path $ProfileRoot "replay-sessions\$Version-$stamp"
if (Test-Path -LiteralPath $sessionRoot) {
    throw 'The unique replay session directory already exists.'
}
New-Item -ItemType Directory -Path $sessionRoot -Force | Out-Null
$networkPath = Join-Path $sessionRoot 'network-observations.jsonl'
$invocation = [ordered]@{
    testVersion = $Version
    environmentProfile = $manifest.launchProfile.name
    environmentProfileSha256 = $manifest.launchProfile.sha256
    onlineRunAuthorized = $true
    elevated = $true
    targetPresent = $TargetPresent
    monitorTimeoutSeconds = $MonitorTimeoutSeconds
    launchArguments = @($manifest.launchProfile.arguments)
    networkMonitorPath = $networkPath
}
$invocation | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $sessionRoot 'run-invocation.json') -Encoding UTF8

$prepareText = & $ProxyManager -Action Prepare -VersionPath $PackageRoot `
    -ManifestPath $ManifestPath -RunRoot $RunRoot -Present $TargetPresent -Confirm:$false
$prepare = ($prepareText | Out-String) | ConvertFrom-Json
if (-not $prepare.ready -or $prepare.mutation_performed) {
    throw 'ProxyManager did not attest the profile without changing the installation.'
}

$existingRunDirectories = @(
    Get-ChildItem -LiteralPath $RunRoot -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { Get-ChildItem -LiteralPath $_.FullName -Directory -ErrorAction SilentlyContinue } |
        ForEach-Object { $_.FullName }
)
$watchCommand = "& '$TcpWatcher' -GameDirectory '$GameRoot' -OutputPath '$networkPath' -DurationSeconds 180 -PostExitSeconds 5"
$watchEncoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($watchCommand))
$powershellExe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$watcher = Start-Process -FilePath $powershellExe `
    -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-EncodedCommand', $watchEncoded) `
    -WindowStyle Hidden -PassThru
$managerError = $null
try {
    Start-Sleep -Milliseconds 700
    if ($watcher.HasExited) {
        throw 'The TCP observer exited before the game launch.'
    }

    & $ProxyManager -Action Run -GameExePath $GameExe -VersionPath $PackageRoot `
        -ManifestPath $ManifestPath -RunRoot $RunRoot -Present $TargetPresent `
        -MonitorTimeoutSeconds $MonitorTimeoutSeconds -Confirm:$false
} catch {
    $managerError = $_.Exception.Message
} finally {
    if (-not $watcher.HasExited -and -not $watcher.WaitForExit(15000)) {
        Stop-Process -Id $watcher.Id -Force
        $null = $watcher.WaitForExit(5000)
    }
}

$newRunDirectories = @(
    Get-ChildItem -LiteralPath $RunRoot -Directory |
        ForEach-Object { Get-ChildItem -LiteralPath $_.FullName -Directory } |
        Where-Object {
            $_.Name.StartsWith($Version + '-', [StringComparison]::OrdinalIgnoreCase) -and
            $existingRunDirectories -notcontains $_.FullName
        }
)
if ($newRunDirectories.Count -ne 1) {
    throw "Expected one new versioned ProxyManager run directory; found $($newRunDirectories.Count). $managerError"
}
$runDirectory = $newRunDirectories[0]

if (Test-Path -LiteralPath $networkPath) {
    Copy-Item -LiteralPath $networkPath -Destination (Join-Path $runDirectory.FullName 'network-observations.jsonl')
}

$managerRunPath = Join-Path $runDirectory.FullName 'manager-run.json'
if (-not (Test-Path -LiteralPath $managerRunPath)) {
    throw "ProxyManager did not write run metadata: $($runDirectory.FullName)"
}
$managerRun = Get-Content -LiteralPath $managerRunPath -Raw | ConvertFrom-Json
$runtimeAfter = @{
    d3d12 = (Get-FileHash -LiteralPath (Join-Path $BinaryRoot 'd3d12.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
    dxgi  = (Get-FileHash -LiteralPath (Join-Path $BinaryRoot 'dxgi.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
}
$remainingGame = @(Get-CimInstance Win32_Process -Filter "Name='Hela-Win64-Shipping.exe' OR Name='crashpad_handler.exe'" |
    Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith($GameRoot, [StringComparison]::OrdinalIgnoreCase) })
$summary = [ordered]@{
    runDirectory = $runDirectory.FullName
    managerStatus = $managerRun.status
    restored = $managerRun.restored
    managerError = if ($managerError) { $managerError } else { $managerRun.error }
    launchProfile = $managerRun.launch_profile
    captureTargetComplete = if (Test-Path -LiteralPath (Join-Path $runDirectory.FullName 'live-monitor-report.json')) {
        (Get-Content -LiteralPath (Join-Path $runDirectory.FullName 'live-monitor-report.json') -Raw | ConvertFrom-Json).capture_target.complete
    } else {
        $false
    }
    runtimeRestored = (($runtimeAfter.d3d12 -eq $ExpectedRuntime.d3d12) -and
        ($runtimeAfter.dxgi -eq $ExpectedRuntime.dxgi))
    remainingGameProcesses = $remainingGame.Count
}
$summary | ConvertTo-Json -Depth 7

if (-not $managerRun.restored -or -not $summary.runtimeRestored -or $remainingGame.Count -gt 0) {
    throw 'Replay cleanup/restoration was not verified; inspect the run directory before continuing.'
}
if ($managerRun.status -ne 'completed') {
    throw "Replay did not complete successfully. See manager-run.json: $($summary.managerError)"
}
