<#
.SYNOPSIS
    Starts LiveFrameMonitor alongside a direct Hela-Win64-Shipping.exe diagnostic run (or in
    standalone monitor/fixture mode) without modifying any DLLs, INIs, or game configuration files.

.DESCRIPTION
    - Manages diagnostic environment variables strictly within the current PowerShell process scope
      and restores their exact prior values in a `finally` block (even on Ctrl-C or errors).
    - Properly quotes Win32 command-line arguments containing spaces (such as `D:\Ghidra MCP\...`
      and `Hela Demo`) when invoking `Start-Process`.
    - Starts `live_frame_monitor.py` before launching `Hela-Win64-Shipping.exe` directly (no Steam)
      with `-graphicsadapter=0 -windowed -ResX=1280 -ResY=720`.
    - Guarantees monitor process teardown when the game exits or when interrupted by the user.
#>
[CmdletBinding()]
param(
    [string]$GameExePath = "D:\SteamLibrary\steamapps\common\Hela Demo\Hela\Binaries\Win64\Hela-Win64-Shipping.exe",
    [string]$LogPath = "",
    [string]$JsonOutPath = "",
    [string]$SourceBmpPath = "",
    [string]$BackbufferBmpPath = "",
    [string]$RunManifestPath = "",
    [UInt32]$MonitorMaxWaitSeconds = 0,
    [switch]$LaunchGame,
    [switch]$EnableReadback = $true,
    [switch]$EnableFullBmpReadback = $false,
    [UInt32]$FullBmpPresent = 100,
    [UInt32]$StopAfterPresent = 0,
    [switch]$StopAfterPresent100 = $false,
    [switch]$StopGameAfterMonitor = $false,
    [switch]$Once = $false,
    [switch]$PrintCommandLineOnly = $false
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($FullBmpPresent -lt 100 -or $FullBmpPresent % 100 -ne 0) {
    throw "-FullBmpPresent must be a positive multiple of 100."
}
if ($StopAfterPresent -ne 0 -and
    ($StopAfterPresent -lt 100 -or $StopAfterPresent % 100 -ne 0)) {
    throw "-StopAfterPresent must be zero or a positive multiple of 100."
}
if ($StopAfterPresent100 -and $StopAfterPresent -ne 0 -and $StopAfterPresent -ne 100) {
    throw "-StopAfterPresent100 conflicts with -StopAfterPresent."
}

function ConvertTo-QuotedWin32Arg {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Arg)
    if ($Arg.Length -eq 0) {
        return '""'
    }
    if ($Arg -notmatch '[\s"]') {
        return $Arg
    }
    # Escape backslashes preceding a quote, escape internal quotes, and escape trailing backslashes
    $escaped = [regex]::Replace($Arg, '(\\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return '"' + $escaped + '"'
}

function Join-Win32CommandLine {
    param([Parameter(Mandatory = $true)][string[]]$ArgsList)
    $quoted = foreach ($item in $ArgsList) {
        ConvertTo-QuotedWin32Arg -Arg $item
    }
    return ($quoted -join ' ')
}

function Get-RunManifest {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Run manifest not found: $Path"
    }
    $manifest = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    if ($manifest.schema -ne "hela-run-manifest/v1" -or
        [string]::IsNullOrWhiteSpace($manifest.testVersion) -or
        $manifest.runtimeAttestation -ne "module-and-shader-sha256" -or
        $null -eq $manifest.runtime -or $null -eq $manifest.overrides -or
        $null -eq $manifest.requiredOverrides) {
        throw "Invalid Hela run manifest: $Path"
    }
    return $manifest
}

function Assert-ExpectedHash {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$ExpectedHash,
        [Parameter(Mandatory = $true)][string]$Role
    )

    if ($ExpectedHash -notmatch '^[0-9a-fA-F]{64}$') {
        throw "Invalid SHA-256 in run manifest for $Role"
    }
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required $Role file not found: $Path"
    }
    $actualHash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    if ($actualHash -ine $ExpectedHash) {
        throw "$Role fingerprint mismatch for '$Path': expected $ExpectedHash, got $actualHash"
    }
}

function Assert-RunManifestMatchesInstallation {
    param([Parameter(Mandatory = $true)]$Manifest)

    foreach ($role in @("d3d12", "dxgi")) {
        $property = $Manifest.runtime.PSObject.Properties[$role]
        if ($null -eq $property) {
            throw "Run manifest is missing runtime.$role"
        }
        Assert-ExpectedHash -Path (Join-Path $GameDir "$role.dll") `
            -ExpectedHash ([string]$property.Value) -Role "$role.dll"
    }

    $overridesDir = Join-Path $GameDir "D3D12On11Patch\overrides"
    if (-not (Test-Path -LiteralPath $overridesDir -PathType Container)) {
        throw "Shader override directory not found: $overridesDir"
    }
    $expectedNames = @(
        $Manifest.overrides.PSObject.Properties |
            ForEach-Object { $_.Name } |
            Sort-Object
    )
    $actualFiles = @(Get-ChildItem -LiteralPath $overridesDir -Filter "*.cso" -File)
    $actualNames = @($actualFiles | ForEach-Object { $_.Name } | Sort-Object)
    if (Compare-Object -ReferenceObject $expectedNames -DifferenceObject $actualNames) {
        throw "Installed shader override set differs from run manifest '$RunManifestPath'"
    }
    foreach ($name in $expectedNames) {
        $property = $Manifest.overrides.PSObject.Properties[$name]
        if ($null -eq $property) {
            throw "Invalid override entry in run manifest: $name"
        }
        Assert-ExpectedHash -Path (Join-Path $overridesDir $name) `
            -ExpectedHash ([string]$property.Value) -Role "shader override $name"
    }
    foreach ($name in @($Manifest.requiredOverrides)) {
        if (-not $Manifest.overrides.PSObject.Properties[$name]) {
            throw "Required shader override is not pinned in the run manifest: $name"
        }
    }
}

function Assert-RunLogMatchesManifest {
    param(
        [Parameter(Mandatory = $true)]$Manifest,
        [Parameter(Mandatory = $true)][string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Run log not found for fingerprint validation: $Path"
    }
    $log = Get-Content -LiteralPath $Path -Raw
    foreach ($role in @("d3d12", "dxgi_proxy")) {
        $expectedHash = [string]$Manifest.runtime.($role -replace "_proxy$", "")
        $pattern = "(?m)^.*MODULE_FINGERPRINT role=$role .*sha256=$([regex]::Escape($expectedHash))(?:\s|$)"
        if ($expectedHash -notmatch '^[0-9a-fA-F]{64}$' -or $log -notmatch $pattern) {
            throw "Run log does not attest the expected $role module fingerprint ($expectedHash)"
        }
    }
    foreach ($name in @($Manifest.requiredOverrides)) {
        $expectedHash = [string]$Manifest.overrides.PSObject.Properties[$name].Value
        $escapedName = [regex]::Escape($name)
        $pattern = "(?m)^.*SHADER READY .*source=SM5-override path=[^\r\n]*$escapedName bytes=\d+ sha256=$([regex]::Escape($expectedHash))(?:\s|$)"
        if ($log -notmatch $pattern) {
            throw "Run log does not attest the expected shader override '$name' ($expectedHash)"
        }
    }
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$MonitorPy = Join-Path $ScriptDir "live_frame_monitor.py"

if (-not (Test-Path -LiteralPath $MonitorPy)) {
    throw "Monitor script not found: $MonitorPy"
}

$GameDir = Split-Path -Parent $GameExePath
if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $LogPath = Join-Path $GameDir "d3d12on11.log"
}

$MonitorArgs = @($MonitorPy, $LogPath)
if ($Once) {
    $MonitorArgs += "--once"
}
if ($StopAfterPresent100) {
    $MonitorArgs += "--stop-after-present100"
}
elseif ($StopAfterPresent -ne 0) {
    $MonitorArgs += @("--stop-after-present", $StopAfterPresent.ToString())
}
if (-not [string]::IsNullOrWhiteSpace($RunManifestPath)) {
    $MonitorArgs += @("--run-manifest", $RunManifestPath)
}
if ($MonitorMaxWaitSeconds -gt 0) {
    $MonitorArgs += @("--max-wait", $MonitorMaxWaitSeconds.ToString())
}
if (-not [string]::IsNullOrWhiteSpace($JsonOutPath)) {
    $MonitorArgs += @("--json", $JsonOutPath)
}
if (-not [string]::IsNullOrWhiteSpace($SourceBmpPath)) {
    $MonitorArgs += @("--source-bmp", $SourceBmpPath)
}
if (-not [string]::IsNullOrWhiteSpace($BackbufferBmpPath)) {
    $MonitorArgs += @("--backbuffer-bmp", $BackbufferBmpPath)
}

$MonitorCmdLine = Join-Win32CommandLine -ArgsList $MonitorArgs

if ($PrintCommandLineOnly) {
    Write-Output $MonitorCmdLine
    exit 0
}

# Verified process-scoped diagnostic environment variables
$ManagedEnvVars = @(
    "D3D12ON11_SHADER_MODEL",
    "D3D12ON11_DIAGNOSTIC_CAPABILITIES",
    "D3D12ON11_TRACE_CALLERS",
    "D3D12ON11_DIAGNOSTIC_RENDER_TRACE",
    "D3D12ON11_TILED_RESOURCES_TIER",
    "D3D12ON11_BACKBUFFER_READBACK",
    "D3D12ON11_FULL_READBACK",
    "D3D12ON11_FULL_READBACK_PRESENT"
)

$SavedEnv = @{}
foreach ($name in $ManagedEnvVars) {
    $SavedEnv[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
}

$MonitorProc = $null
$GameProc = $null

try {
    if (-not $LaunchGame) {
        Write-Host "[Launcher] Running LiveFrameMonitor in monitor-only mode on: $LogPath" -ForegroundColor Cyan
        & python @MonitorArgs
        exit $LASTEXITCODE
    }

        if ([string]::IsNullOrWhiteSpace($RunManifestPath)) {
            throw "-RunManifestPath is required when -LaunchGame is specified."
        }
        $RunManifest = Get-RunManifest -Path $RunManifestPath
        Assert-RunManifestMatchesInstallation -Manifest $RunManifest
        Write-Host "[Launcher] Run manifest verified: $($RunManifest.testVersion)" -ForegroundColor Green

        if (-not (Test-Path -LiteralPath $GameExePath)) {
            throw "Game executable not found at: $GameExePath"
    }

    # Set process-scoped diagnostic environment variables for the child game process
    [Environment]::SetEnvironmentVariable("D3D12ON11_SHADER_MODEL", "6_7", "Process")
    [Environment]::SetEnvironmentVariable("D3D12ON11_DIAGNOSTIC_CAPABILITIES", "1", "Process")
    [Environment]::SetEnvironmentVariable("D3D12ON11_TRACE_CALLERS", "1", "Process")
    [Environment]::SetEnvironmentVariable("D3D12ON11_DIAGNOSTIC_RENDER_TRACE", "1", "Process")
    [Environment]::SetEnvironmentVariable("D3D12ON11_TILED_RESOURCES_TIER", "0", "Process")
    [Environment]::SetEnvironmentVariable("D3D12ON11_BACKBUFFER_READBACK", ($(if ($EnableReadback) { "1" } else { "0" })), "Process")
    [Environment]::SetEnvironmentVariable("D3D12ON11_FULL_READBACK", ($(if ($EnableFullBmpReadback) { "1" } else { "0" })), "Process")
    [Environment]::SetEnvironmentVariable("D3D12ON11_FULL_READBACK_PRESENT", $FullBmpPresent.ToString(), "Process")

    Write-Host "[Launcher] Starting LiveFrameMonitor background process..." -ForegroundColor Cyan
    # Pass a single properly-quoted Win32 command-line string so Start-Process never splits paths with spaces
    $MonitorProc = Start-Process -FilePath "python" -ArgumentList $MonitorCmdLine -NoNewWindow -PassThru

    $GameArgs = @("-graphicsadapter=0", "-windowed", "-ResX=1280", "-ResY=720")
    $GameCmdLine = Join-Win32CommandLine -ArgsList $GameArgs
    Write-Host "[Launcher] Launching Hela directly (no Steam): $GameExePath $GameCmdLine" -ForegroundColor Green
    $GameProc = Start-Process -FilePath $GameExePath -ArgumentList $GameCmdLine -WorkingDirectory $GameDir -PassThru

    while (-not $GameProc.HasExited) {
        if ($MonitorProc.HasExited) {
            break
        }
        Start-Sleep -Milliseconds 250
    }
}
finally {
    # 1. Tear down monitor process if still running
    if ($null -ne $MonitorProc -and -not $MonitorProc.HasExited) {
        Write-Host "[Launcher] Stopping LiveFrameMonitor (PID $($MonitorProc.Id))..." -ForegroundColor Yellow
        Stop-Process -Id $MonitorProc.Id -Force -ErrorAction Stop
    }

    if ($StopGameAfterMonitor -and $null -ne $GameProc -and -not $GameProc.HasExited) {
        Write-Host "[Launcher] Closing diagnostic game process (PID $($GameProc.Id))..." -ForegroundColor Yellow
        $null = $GameProc.CloseMainWindow()
        if (-not $GameProc.WaitForExit(5000)) {
            Stop-Process -Id $GameProc.Id -Force -ErrorAction Stop
            $GameProc.WaitForExit()
        }
    }

    # 2. Restore strictly process-scoped environment variables
    foreach ($name in $ManagedEnvVars) {
        [Environment]::SetEnvironmentVariable($name, $SavedEnv[$name], "Process")
    }
    Write-Host "[Launcher] Process-scoped environment variables restored." -ForegroundColor DarkGray
}

if ($LaunchGame) {
    Assert-RunLogMatchesManifest -Manifest $RunManifest -Path $LogPath
    Write-Host "[Launcher] Runtime DLL and shader fingerprints match $($RunManifest.testVersion)." -ForegroundColor Green
}
