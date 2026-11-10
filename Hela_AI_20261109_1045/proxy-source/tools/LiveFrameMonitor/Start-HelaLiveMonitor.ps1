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
    [switch]$LaunchGame,
    [switch]$EnableReadback = $true,
    [switch]$EnableFullBmpReadback = $false,
    [switch]$StopAfterPresent100 = $false,
    [switch]$Once = $false,
    [switch]$PrintCommandLineOnly = $false
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

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
    "D3D12ON11_FULL_READBACK"
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
        try {
            Stop-Process -Id $MonitorProc.Id -Force -ErrorAction SilentlyContinue
        } catch {}
    }

    # 2. Restore strictly process-scoped environment variables
    foreach ($name in $ManagedEnvVars) {
        [Environment]::SetEnvironmentVariable($name, $SavedEnv[$name], "Process")
    }
    Write-Host "[Launcher] Process-scoped environment variables restored." -ForegroundColor DarkGray
}
