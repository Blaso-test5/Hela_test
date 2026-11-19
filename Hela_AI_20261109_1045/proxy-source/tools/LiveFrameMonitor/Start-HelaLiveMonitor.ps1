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
    [string]$LaunchEnvironmentProfileName = "",
    [string]$LaunchEnvironmentProfileSha256 = "",
    [hashtable]$LaunchEnvironment = @{},
    [string[]]$LaunchArguments = @(),
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

function Get-LaunchEnvironmentSha256 {
    param([Parameter(Mandatory = $true)]$Environment)

    $lines = foreach ($property in @($Environment.PSObject.Properties | Sort-Object Name)) {
        "$($property.Name)=$([string]$property.Value)"
    }
    $canonical = (($lines -join "`n") + "`n")
    $bytes = [Text.Encoding]::UTF8.GetBytes($canonical)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace("-", "").ToLowerInvariant()
    } finally {
        $sha.Dispose()
    }
}

function Assert-LaunchProfileMatchesManifest {
    param([Parameter(Mandatory = $true)]$Manifest)

    $profileProperty = $Manifest.PSObject.Properties["launchProfile"]
    if ($null -eq $profileProperty) {
        if (-not [string]::IsNullOrWhiteSpace($LaunchEnvironmentProfileName) -or
            -not [string]::IsNullOrWhiteSpace($LaunchEnvironmentProfileSha256) -or
            $LaunchEnvironment.Count -gt 0 -or $LaunchArguments.Count -gt 0) {
            throw "Launch environment was supplied for a manifest without launchProfile."
        }
        return
    }

    $profile = $profileProperty.Value
    if ($LaunchEnvironmentProfileName -cne [string]$profile.name -or
        $LaunchEnvironmentProfileSha256 -cne [string]$profile.sha256) {
        throw "Launch profile identity does not match the run manifest."
    }
    if ((Get-LaunchEnvironmentSha256 -Environment $profile.environment) -ne
        [string]$profile.sha256) {
        throw "Launch profile environment fingerprint does not match the run manifest."
    }
    $profileEnvironment = @($profile.environment.PSObject.Properties)
    if ($LaunchEnvironment.Count -ne $profileEnvironment.Count) {
        throw "Resolved child environment does not match the run manifest launch profile."
    }
    $runDirectory = Split-Path -Parent ([IO.Path]::GetFullPath($RunManifestPath))
    foreach ($property in $LaunchEnvironment.GetEnumerator()) {
        if ($property.Key -notmatch '^D3D12ON11_[A-Z0-9_]+$' -or
            $property.Value -isnot [string] -or
            [string]$property.Value -match '[\r\n]') {
            throw "Invalid child launch environment entry: $($property.Key)"
        }
    }
    foreach ($property in $profileEnvironment) {
        $actualProperty = $LaunchEnvironment.GetEnumerator() |
            Where-Object { $_.Key -ceq $property.Name } |
            Select-Object -First 1
        if ($null -eq $actualProperty) {
            throw "Resolved child environment is missing $($property.Name)."
        }
        $expectedValue = [string]$property.Value
        if ($expectedValue -ceq "@RUN_OVERRIDES@") {
            $expectedValue = Join-Path $runDirectory "launch-profile\overrides"
        } elseif ($expectedValue -ceq "@RUN_DUMP@") {
            $expectedValue = Join-Path $runDirectory "launch-profile\dump"
        }
        $matchesExpected = if ($property.Value -ceq "@RUN_OVERRIDES@" -or
            $property.Value -ceq "@RUN_DUMP@") {
            [string]$actualProperty.Value -ieq $expectedValue
        } else {
            [string]$actualProperty.Value -ceq $expectedValue
        }
        if (-not $matchesExpected) {
            throw "Resolved child environment value does not match the launch profile for $($property.Name)."
        }
    }
    if ($LaunchArguments.Count -ne @($profile.arguments).Count) {
        throw "Launch arguments do not match the run manifest launch profile."
    }
    for ($index = 0; $index -lt $LaunchArguments.Count; $index++) {
        if ($LaunchArguments[$index] -cne [string]$profile.arguments[$index]) {
            throw "Launch argument $index does not match the run manifest launch profile."
        }
    }
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
    $profileProperty = $Manifest.PSObject.Properties["launchProfile"]
    if ($null -ne $profileProperty) {
        $attestation = $profileProperty.Value.attestation
        if ($attestation.maskedNvidiaAdapter -eq $true) {
            if ($log -notmatch '(?m)^.*DIAGNOSTIC_MASK_NVIDIA: .*exposed-vendor=0xffff(?:\s|$)' -or
                $log -notmatch '(?m)^.*D3D12CreateDevice adapter source=proxy-unwrap(?:\s|$)') {
                throw "Run log does not attest the required masked NVIDIA proxy adapter."
            }
        }
        if ($null -ne $attestation.tiledResourcesTier) {
            $tier = [string]$attestation.tiledResourcesTier
            $pattern = "(?m)^.*CheckFeatureSupport end feature=D3D12_OPTIONS(?:\(0\))?.*tiled-tier=$tier(?:\s|$)"
            if ($log -notmatch $pattern) {
                throw "Run log does not attest the required tiled-resource tier $tier."
            }
        }
        if ($null -ne $attestation.callerTrace) {
            $traceValue = if ($attestation.callerTrace) { "1" } else { "0" }
            if ($log -notmatch "(?m)^.*Config caller-trace=$traceValue(?:\s|$)") {
                throw "Run log does not attest the required caller-trace setting $traceValue."
            }
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
$MonitorStartedAt = $null
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
        Assert-LaunchProfileMatchesManifest -Manifest $RunManifest
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
    $MonitorStartedAt = [DateTime]::UtcNow
    $MonitorProc = Start-Process -FilePath "python" -ArgumentList $MonitorCmdLine -NoNewWindow -PassThru

    $GameArgs = if ($LaunchArguments.Count -gt 0) {
        $LaunchArguments
    } else {
        @("-graphicsadapter=0", "-windowed", "-ResX=1280", "-ResY=720")
    }
    $GameCmdLine = Join-Win32CommandLine -ArgsList $GameArgs
    Write-Host "[Launcher] Launching Hela directly (no Steam): $GameExePath $GameCmdLine" -ForegroundColor Green
    if (-not [string]::IsNullOrWhiteSpace($LaunchEnvironmentProfileName)) {
        $gameStartInfo = [System.Diagnostics.ProcessStartInfo]::new()
        $gameStartInfo.FileName = $GameExePath
        $gameStartInfo.WorkingDirectory = $GameDir
        $gameStartInfo.Arguments = $GameCmdLine
        $gameStartInfo.UseShellExecute = $false
        foreach ($name in @($gameStartInfo.EnvironmentVariables.Keys)) {
            if ($name -like "D3D12ON11_*") {
                $gameStartInfo.EnvironmentVariables.Remove($name) | Out-Null
            }
        }
        foreach ($name in $LaunchEnvironment.Keys) {
            $gameStartInfo.EnvironmentVariables[$name] = [string]$LaunchEnvironment[$name]
        }
        $gameStartInfo.EnvironmentVariables["D3D12ON11_BACKBUFFER_READBACK"] =
            $(if ($EnableReadback) { "1" } else { "0" })
        $gameStartInfo.EnvironmentVariables["D3D12ON11_FULL_READBACK"] =
            $(if ($EnableFullBmpReadback) { "1" } else { "0" })
        $gameStartInfo.EnvironmentVariables["D3D12ON11_FULL_READBACK_PRESENT"] =
            $FullBmpPresent.ToString()
        Write-Host "[Launcher] Child environment profile: $LaunchEnvironmentProfileName sha256=$LaunchEnvironmentProfileSha256 (inherited D3D12ON11_* cleared)" -ForegroundColor Cyan
        $GameProc = [System.Diagnostics.Process]::Start($gameStartInfo)
        if ($null -eq $GameProc) {
            throw "Failed to start Hela with launch profile '$LaunchEnvironmentProfileName'."
        }
    } else {
        $GameProc = Start-Process -FilePath $GameExePath -ArgumentList $GameCmdLine -WorkingDirectory $GameDir -PassThru
    }

    while (-not $GameProc.HasExited) {
        if ($MonitorProc.HasExited) {
            break
        }
        Start-Sleep -Milliseconds 250
    }
}
finally {
    try {
        if ($null -ne $MonitorProc -and -not $MonitorProc.HasExited) {
            if ($null -ne $GameProc -and
                $GameProc.HasExited -and
                $MonitorMaxWaitSeconds -gt 0 -and
                $null -ne $MonitorStartedAt) {
                $monitorDeadline = $MonitorStartedAt.AddSeconds(
                    [double]$MonitorMaxWaitSeconds + 5.0)
                $remainingMilliseconds = [int][Math]::Max(
                    0,
                    [Math]::Min(
                        [int]::MaxValue,
                        [Math]::Ceiling(($monitorDeadline - [DateTime]::UtcNow).TotalMilliseconds)))
                if ($remainingMilliseconds -gt 0) {
                    Write-Host "[Launcher] Hela exited; allowing the monitor to finalize its bounded report..." -ForegroundColor Yellow
                    $null = $MonitorProc.WaitForExit($remainingMilliseconds)
                }
            }

            if (-not $MonitorProc.HasExited) {
                Write-Host "[Launcher] Stopping LiveFrameMonitor (PID $($MonitorProc.Id))..." -ForegroundColor Yellow
                Stop-Process -Id $MonitorProc.Id -Force -ErrorAction SilentlyContinue
                if (-not $MonitorProc.WaitForExit(5000)) {
                    throw "LiveFrameMonitor process $($MonitorProc.Id) did not exit after termination."
                }
            }
        }
    } finally {
        try {
            if ($StopGameAfterMonitor -and $null -ne $GameProc -and -not $GameProc.HasExited) {
                Write-Host "[Launcher] Closing diagnostic game process (PID $($GameProc.Id))..." -ForegroundColor Yellow
                try {
                    $null = $GameProc.CloseMainWindow()
                } catch [InvalidOperationException] {
                    # The game may exit between HasExited and CloseMainWindow.
                }
                if (-not $GameProc.WaitForExit(5000)) {
                    Stop-Process -Id $GameProc.Id -Force -ErrorAction SilentlyContinue
                    if (-not $GameProc.WaitForExit(5000)) {
                        throw "Hela process $($GameProc.Id) did not exit after termination."
                    }
                }
            }
        } finally {
            foreach ($name in $ManagedEnvVars) {
                [Environment]::SetEnvironmentVariable($name, $SavedEnv[$name], "Process")
            }
            Write-Host "[Launcher] Process-scoped environment variables restored." -ForegroundColor DarkGray
        }
    }
}

if ($LaunchGame) {
    Assert-RunLogMatchesManifest -Manifest $RunManifest -Path $LogPath
    Write-Host "[Launcher] Runtime DLL and shader fingerprints match $($RunManifest.testVersion)." -ForegroundColor Green
}
