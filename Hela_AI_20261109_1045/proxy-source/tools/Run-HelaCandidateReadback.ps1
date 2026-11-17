[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$GameDirectory,
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$RunDirectory,
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$ExternalReadbackDirectory,
    [ValidateRange(100, 100000)]
    [int]$TargetPresent = 1400,
    [ValidateRange(60, 300)]
    [int]$TimeoutSeconds = 120,
    [ValidateRange(0, 120)]
    [int]$ObserveAfterReadbackSeconds = 0,
    [string[]]$AdditionalCandidateHashes = @(),
    [switch]$PreflightOnly
)

$ErrorActionPreference = 'Stop'

$runRoot = [System.IO.Path]::GetFullPath($RunDirectory)
$diagnosticName = 'powershell-error-' + [System.DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '.json'
$diagnosticPath = Join-Path $runRoot $diagnosticName
$diagnosticStage = 'initialization'
try {
$gameRoot = [System.IO.Path]::GetFullPath($GameDirectory)
$externalReadbackRoot = [System.IO.Path]::GetFullPath($ExternalReadbackDirectory)
$proxyReadbackRoot = [System.IO.Path]::GetPathRoot('D:\')
$proxySourceRoot = Split-Path -Parent $PSScriptRoot
$packageRoot = Split-Path -Parent $proxySourceRoot
$gameBin = Join-Path $gameRoot 'Binaries\Win64'
$gameExe = Join-Path $gameBin 'Hela-Win64-Shipping.exe'
$crashpadExe = Join-Path $gameRoot 'Plugins\Sentry\Binaries\Win64\crashpad_handler.exe'
$blockScript = Join-Path $PSScriptRoot 'Block-HelaEgress.ps1'
$watchScript = Join-Path $PSScriptRoot 'Watch-HelaTcp.ps1'
$overrides = Join-Path $runRoot 'baseline\overrides'
$dumpDirectory = Join-Path $runRoot 'baseline\dump'
$gameLog = Join-Path $gameBin 'd3d12on11.log'
$networkLog = Join-Path $runRoot 'network-observations.jsonl'
$runEvidenceModule = Join-Path $PSScriptRoot 'HelaRunEvidence.psm1'
$preState = Join-Path $runRoot 'pre-state'
$externalReadbackNames = @(
    'HelaReadback_Present100_source.bmp',
    'HelaReadback_Present100_backbuffer.bmp',
    "HelaReadback_Present$TargetPresent`_source.bmp",
    "HelaReadback_Present$TargetPresent`_backbuffer.bmp"
)
$baselineRun = Join-Path $packageRoot 'evidence\GTX_1050TI\runs\20261116\placed-resource-overlap-runtime-20261116-132423\baseline\overrides'
$candidateRun = Join-Path $packageRoot 'evidence\GTX_1050TI\runs\20261116\placed-resource-overlap-monitored-20261116-132140\analysis\local-cs-sm5-candidates'
$additionalCandidateRun = Join-Path $packageRoot 'shaders\overrides'
$expectedDllHashes = @{
    'd3d12.dll' = '14DFBA07E9A37B1C8ADBC7FE11DE6BA14EE0AB3968E953B85A097D32DBF69E98'
    'dxgi.dll' = '09D13C05BF198C74B4190A9CB1BE1C96966A0ECE7641D8621C08A00465E0F857'
}

function Get-ProcessPaths {
    return @(
        Get-CimInstance -ClassName Win32_Process `
            -Filter "Name='Hela-Win64-Shipping.exe' OR Name='crashpad_handler.exe'" |
            Where-Object {
                $_.ExecutablePath -and
                $_.ExecutablePath.StartsWith(
                    $gameRoot,
                    [System.StringComparison]::OrdinalIgnoreCase)
            }
    )
}

function Get-OverrideManifest {
    param([Parameter(Mandatory = $true)][string]$Directory)

    if (-not [System.IO.Directory]::Exists($Directory)) {
        return @()
    }

    return @(
        Get-ChildItem -LiteralPath $Directory -File -Filter '*.cso' |
            Sort-Object Name |
            ForEach-Object {
                [pscustomobject]@{
                    name = $_.Name
                    sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
                }
            }
    )
}

function Get-QuotedArgument {
    param([Parameter(Mandatory = $true)][string]$Value)

    return '"' + $Value.Replace('"', '\"') + '"'
}

function Get-SanitizedLogText {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$Text
    )

    $safeText = [regex]::Replace($Text, '(?i)(https?://)[^/@\s]+@', '$1[REDACTED]@')
    $safeText = [regex]::Replace($safeText, '(?i)(sentry[_-]?key\s*[=:]\s*)[^\s&]+', '$1[REDACTED]')
    $safeText = [regex]::Replace($safeText, '(?i)(dsn\s*[=:]\s*)[^\s,;]+', '$1[REDACTED]')
    $safeText = [regex]::Replace($safeText, '(?i)(authorization:\s*(?:bearer|basic)\s+)[^\s]+', '$1[REDACTED]')
    return $safeText
}

if (-not [System.IO.File]::Exists($gameExe) -or
    -not [System.IO.File]::Exists($crashpadExe) -or
    -not [System.IO.File]::Exists($blockScript) -or
    -not [System.IO.File]::Exists($watchScript) -or
    -not [System.IO.File]::Exists($runEvidenceModule)) {
    throw 'Required Hela executable or safety tool is missing.'
}
$diagnosticStage = 'module-load'
Import-Module -Name $runEvidenceModule -Force
$diagnosticStage = 'run-directory-validation'
if (-not [System.IO.Directory]::Exists($RunDirectory) -or
    -not [System.IO.Directory]::Exists($overrides) -or
    -not [System.IO.Directory]::Exists($dumpDirectory)) {
    throw "Prepared run directories are missing: $RunDirectory"
}
if (-not [System.IO.Directory]::Exists($externalReadbackRoot)) {
    throw "External readback directory does not exist: $externalReadbackRoot"
}
if (-not [System.String]::Equals(
        $externalReadbackRoot,
        $proxyReadbackRoot,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "The current proxy writes full-frame readbacks to $proxyReadbackRoot."
}
if ($TargetPresent -lt 100 -or ($TargetPresent % 100) -ne 0) {
    throw 'TargetPresent must be a positive multiple of 100.'
}

$diagnosticStage = 'firewall-verification'
$powershellExe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$previousErrorActionPreference = $ErrorActionPreference
try {
    $ErrorActionPreference = 'Continue'
    $blockOutput = @(
        & $powershellExe -NoProfile -ExecutionPolicy Bypass -File $blockScript `
            -GameDirectory $gameRoot 2>&1
    )
    $blockExitCode = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = $previousErrorActionPreference
}
$blockOutput | ForEach-Object { Write-Output $_ }
if ($blockExitCode -ne 0) {
    $helperError = ($blockOutput | ForEach-Object { [string]$_ }) -join [System.Environment]::NewLine
    throw "The effective firewall block did not verify in this PowerShell context (helper exit code $blockExitCode); Hela was not started. $helperError"
}
$blockVerification = $null
$verificationPrefix = 'HELA_EGRESS_VERIFICATION '
foreach ($line in $blockOutput) {
    $text = [string]$line
    if ($text.StartsWith($verificationPrefix, [System.StringComparison]::Ordinal)) {
        $blockVerification = ConvertFrom-Json -InputObject $text.Substring($verificationPrefix.Length) -ErrorAction Stop
        break
    }
}
if (-not (Test-HelaFirewallVerification `
            -Verification $blockVerification `
            -ExpectedPrograms @($gameExe, $crashpadExe))) {
    throw 'The firewall helper did not confirm both expected executable rules in ActiveStore; Hela was not started.'
}

$diagnosticStage = 'gpu-verification'
$gpu = @(
    Get-CimInstance Win32_VideoController |
        Where-Object { $_.Name -eq 'NVIDIA GeForce GTX 1050 Ti' }
)
if ($gpu.Count -eq 0) {
    throw 'GTX 1050 Ti was not found; refusing to write evidence under the wrong GPU directory.'
}

$diagnosticStage = 'override-validation'
$requiredCandidateHashes = @(
    'bb592abf5ce2bad1',
    'c71574a8f462e2c1',
    'df79d6ce6e6eaa48',
    '5e8318486d1b2a98'
)
$additionalHashes = @(
    $AdditionalCandidateHashes |
        ForEach-Object { ([string]$_).ToLowerInvariant() }
)
if (@($additionalHashes | Where-Object { $_ -notmatch '^[0-9a-f]{16}$' }).Count -gt 0 -or
    @($additionalHashes | Sort-Object -Unique).Count -ne $additionalHashes.Count -or
    @($additionalHashes | Where-Object { $requiredCandidateHashes -contains $_ }).Count -gt 0) {
    throw 'AdditionalCandidateHashes must contain unique 16-digit shader hashes not already in the required candidate set.'
}
$stagedFiles = @(Get-ChildItem -LiteralPath $overrides -File -Filter '*.cso')
$expectedOverrideCount = 12 + $additionalHashes.Count
if ($stagedFiles.Count -ne $expectedOverrideCount) {
    throw "Expected $expectedOverrideCount staged overrides, found $($stagedFiles.Count)."
}
$sourceDirectories = @($baselineRun, $candidateRun)
if ($additionalHashes.Count -gt 0) {
    $sourceDirectories += $additionalCandidateRun
}
foreach ($sourceDirectory in $sourceDirectories) {
    if (-not [System.IO.Directory]::Exists($sourceDirectory)) {
        throw "Override source directory is missing: $sourceDirectory"
    }
}
foreach ($source in @(Get-ChildItem -LiteralPath $baselineRun -File -Filter '*.cso')) {
    $stagedPath = Join-Path $overrides $source.Name
    if (-not [System.IO.File]::Exists($stagedPath) -or
        (Get-FileHash -LiteralPath $source.FullName -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $stagedPath -Algorithm SHA256).Hash) {
        throw "Baseline override is missing or changed: $($source.Name)"
    }
}
foreach ($hash in $requiredCandidateHashes) {
    $candidatePath = Join-Path $candidateRun "$hash.cso"
    $stagedPath = Join-Path $overrides "$hash.cso"
    if (-not [System.IO.File]::Exists($candidatePath) -or
        -not [System.IO.File]::Exists($stagedPath) -or
        (Get-FileHash -LiteralPath $candidatePath -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $stagedPath -Algorithm SHA256).Hash) {
        throw "Candidate override is missing or changed: $hash.cso"
    }
}
foreach ($hash in $additionalHashes) {
    $candidatePath = Join-Path $additionalCandidateRun "$hash.cso"
    $stagedPath = Join-Path $overrides "$hash.cso"
    if (-not [System.IO.File]::Exists($candidatePath) -or
        -not [System.IO.File]::Exists($stagedPath) -or
        (Get-FileHash -LiteralPath $candidatePath -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $stagedPath -Algorithm SHA256).Hash) {
        throw "Additional candidate override is missing or changed: $hash.cso"
    }
}

$dllHashesBefore = @{}
$diagnosticStage = 'runtime-validation'
foreach ($dllName in $expectedDllHashes.Keys) {
    $dllPath = Join-Path $gameBin $dllName
    if (-not [System.IO.File]::Exists($dllPath)) {
        throw "Expected installed proxy DLL is missing: $dllPath"
    }
    $hash = (Get-FileHash -LiteralPath $dllPath -Algorithm SHA256).Hash
    if ($hash -ne $expectedDllHashes[$dllName]) {
        throw "Installed $dllName hash differs from the verified run profile."
    }
    $dllHashesBefore[$dllName] = $hash
}

$diagnosticStage = 'run-output-validation'
if ((Get-ProcessPaths).Count -gt 0) {
    throw 'Hela or Crashpad is already running from this game directory.'
}
foreach ($outputName in @('stdout-sanitized.txt', 'stderr-sanitized.txt', 'd3d12on11.log', 'restore-state.json', 'network-observations.jsonl')) {
    if ([System.IO.File]::Exists((Join-Path $RunDirectory $outputName))) {
        throw "Run output already exists; refusing to overwrite: $outputName"
    }
}
foreach ($name in $externalReadbackNames) {
    if ([System.IO.File]::Exists((Join-Path $RunDirectory $name))) {
        throw "Run-local readback already exists; refusing to overwrite: $name"
    }
}
if ([System.IO.Directory]::Exists($preState)) {
    throw "Pre-state directory already exists; refusing to overwrite: $preState"
}

if ($PreflightOnly) {
    [ordered]@{
        status = 'preflight-passed'
        gameStarted = $false
        gameDirectory = $gameRoot
        runDirectory = $runRoot
        gpu = $gpu[0].Name
        stagedOverrideCount = $stagedFiles.Count
        dllHashes = $dllHashesBefore
        firewall = $blockVerification
    } | ConvertTo-Json -Depth 6
    return
}

$sharedOverrides = Join-Path $gameBin 'D3D12On11Patch\overrides'
$sharedOverrideManifest = @(Get-OverrideManifest -Directory $sharedOverrides)
$originalLogExists = [System.IO.File]::Exists($gameLog)
$originalReadbacks = @{}
$snapshotsReady = $false
$gameProcess = $null
$watcherProcess = $null
$stdoutTask = $null
$stderrTask = $null
$runError = $null
$timeoutReached = $false
$targetReadbackReady = $false
$targetReadbackLogged = $false
$menuObservation = [ordered]@{
    requestedSeconds = $ObserveAfterReadbackSeconds
    attempted = $false
    elapsedSeconds = 0
    processExitedBeforeWindow = $null
    processExitedDuringWindow = $null
    menuReached = 'undetermined'
    menuSignalMatched = $false
}
$gameExitCode = $null
$presentCount = 0
$readbackFiles = @()
$restoreErrors = @()

$diagnosticStage = 'run-and-restoration'
try {
    [System.IO.Directory]::CreateDirectory($preState) | Out-Null
    $preStateReadbacks = Join-Path $preState 'external-readbacks'
    [System.IO.Directory]::CreateDirectory($preStateReadbacks) | Out-Null
    if ($originalLogExists) {
        Copy-Item -LiteralPath $gameLog -Destination (Join-Path $preState 'd3d12on11.log')
    }
    foreach ($name in $externalReadbackNames) {
        $source = Join-Path $externalReadbackRoot $name
        if ([System.IO.File]::Exists($source)) {
            Copy-Item -LiteralPath $source -Destination (Join-Path $preStateReadbacks $name)
            $originalReadbacks[$name] = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
        }
    }
    $snapshotsReady = $true

    if ([System.IO.File]::Exists($gameLog)) {
        Remove-Item -LiteralPath $gameLog -Force
    }
    foreach ($name in $externalReadbackNames) {
        $source = Join-Path $externalReadbackRoot $name
        if ([System.IO.File]::Exists($source)) {
            Remove-Item -LiteralPath $source -Force
        }
    }

    $watchArguments = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', (Get-QuotedArgument $watchScript),
        '-GameDirectory', (Get-QuotedArgument $gameRoot),
        '-OutputPath', (Get-QuotedArgument $networkLog),
        '-DurationSeconds', [string]($TimeoutSeconds + $ObserveAfterReadbackSeconds + 30),
        '-PostExitSeconds', '5'
    ) -join ' '
    $watcherProcess = Start-Process -FilePath $powershellExe `
        -ArgumentList $watchArguments -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 500
    if ($watcherProcess.HasExited) {
        throw 'TCP monitor exited before Hela started.'
    }

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $gameExe
    $startInfo.WorkingDirectory = $gameBin
    $startInfo.Arguments = '-graphicsadapter=0 -windowed -ResX=1280 -ResY=720 -stdout -FullStdOutLogOutput -forcelogflush'
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($key in @($startInfo.EnvironmentVariables.AllKeys)) {
        if ($key -like 'D3D12ON11_*') {
            $startInfo.EnvironmentVariables.Remove($key)
        }
    }
    $environment = @{
        D3D12ON11_SHADER_MODEL = '6_7'
        D3D12ON11_DIAGNOSTIC_CAPABILITIES = '1'
        D3D12ON11_DIAGNOSTIC_RENDER_TRACE = '1'
        D3D12ON11_DIAGNOSTIC_MASK_NVIDIA = '1'
        D3D12ON11_MASK_VENDOR_ID = 'ffff'
        D3D12ON11_BACKBUFFER_READBACK = '1'
        D3D12ON11_FULL_READBACK = '1'
        D3D12ON11_FULL_READBACK_PRESENT = [string]$TargetPresent
        D3D12ON11_DUMP_SHADERS = '0'
        D3D12ON11_DUMP = $dumpDirectory
        D3D12ON11_OVERRIDES = $overrides
    }
    foreach ($key in $environment.Keys) {
        $startInfo.EnvironmentVariables[$key] = $environment[$key]
    }

    $gameProcess = [System.Diagnostics.Process]::Start($startInfo)
    $stdoutTask = $gameProcess.StandardOutput.ReadToEndAsync()
    $stderrTask = $gameProcess.StandardError.ReadToEndAsync()
    $deadline = [System.DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $sourcePath = Join-Path $externalReadbackRoot "HelaReadback_Present$($TargetPresent)_source.bmp"
    $backbufferPath = Join-Path $externalReadbackRoot "HelaReadback_Present$($TargetPresent)_backbuffer.bmp"
    $previousSourceLength = -1L
    $previousBackbufferLength = -1L
    $stableReadbackPolls = 0

    while ([System.DateTime]::UtcNow -lt $deadline) {
        if ($gameProcess.HasExited) {
            break
        }
        if ([System.IO.File]::Exists($sourcePath) -and
            [System.IO.File]::Exists($backbufferPath)) {
            $sourceLength = (Get-Item -LiteralPath $sourcePath).Length
            $backbufferLength = (Get-Item -LiteralPath $backbufferPath).Length
            if ($sourceLength -gt 0 -and $backbufferLength -gt 0 -and
                $sourceLength -eq $previousSourceLength -and
                $backbufferLength -eq $previousBackbufferLength) {
                $stableReadbackPolls++
            }
            else {
                $stableReadbackPolls = 0
            }
            if ($stableReadbackPolls -ge 2) {
                $targetReadbackReady = $true
                break
            }
            $previousSourceLength = $sourceLength
            $previousBackbufferLength = $backbufferLength
        }
        else {
            $stableReadbackPolls = 0
            $previousSourceLength = -1L
            $previousBackbufferLength = -1L
        }
        Start-Sleep -Milliseconds 500
    }

    if (-not $gameProcess.HasExited -and -not $targetReadbackReady) {
        $timeoutReached = $true
    }
    if ($ObserveAfterReadbackSeconds -gt 0 -and $targetReadbackReady) {
        if ($gameProcess.HasExited) {
            $menuObservation.processExitedBeforeWindow = $true
        }
        else {
            $menuObservation.attempted = $true
            $observationStartedUtc = [System.DateTime]::UtcNow
            $observationDeadline = $observationStartedUtc.AddSeconds($ObserveAfterReadbackSeconds)
            while ([System.DateTime]::UtcNow -lt $observationDeadline) {
                if ($gameProcess.HasExited) {
                    break
                }
                Start-Sleep -Milliseconds 500
            }
            $menuObservation.elapsedSeconds = [Math]::Round(
                ([System.DateTime]::UtcNow - $observationStartedUtc).TotalSeconds,
                1)
            $menuObservation.processExitedDuringWindow = $gameProcess.HasExited
        }
    }
    if (-not $gameProcess.HasExited) {
        $null = $gameProcess.CloseMainWindow()
        if (-not $gameProcess.WaitForExit(10000)) {
            Stop-Process -Id $gameProcess.Id -Force
            $gameProcess.WaitForExit()
        }
    }
    $gameExitCode = $gameProcess.ExitCode

    if ([System.IO.File]::Exists($gameLog)) {
        Copy-Item -LiteralPath $gameLog -Destination (Join-Path $RunDirectory 'd3d12on11.log')
        $runLogText = [System.IO.File]::ReadAllText((Join-Path $RunDirectory 'd3d12on11.log'))
        $presentCount = [regex]::Matches(
            $runLogText,
            'IDXGISwapChain::Present hr=0x00000000').Count
        $sourcePattern = "\[READBACK_FULL\]\s+Present #$TargetPresent saved READBACK_SRC path="
        $backbufferPattern = "\[READBACK_FULL\]\s+Present #$TargetPresent saved READBACK path="
        $targetReadbackLogged =
            ($runLogText -match $sourcePattern) -and
            ($runLogText -match $backbufferPattern)
    }
    $targetReadbackReady = $targetReadbackReady -and $targetReadbackLogged
    foreach ($name in $externalReadbackNames) {
        $source = Join-Path $externalReadbackRoot $name
        if ([System.IO.File]::Exists($source)) {
            $destination = Join-Path $RunDirectory $name
            Copy-Item -LiteralPath $source -Destination $destination
            $readbackFiles += [pscustomobject]@{
                name = $name
                length = (Get-Item -LiteralPath $destination).Length
                sha256 = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
            }
        }
    }
}
catch {
    $runError = $_.Exception.ToString()
}
finally {
    if ($gameProcess -and -not $gameProcess.HasExited) {
        try {
            $null = $gameProcess.CloseMainWindow()
            if (-not $gameProcess.WaitForExit(10000)) {
                Stop-Process -Id $gameProcess.Id -Force
            }
            $gameProcess.WaitForExit()
        }
        catch {
            $restoreErrors += "Could not stop the test game process: $($_.Exception.Message)"
        }
    }
    if ($gameProcess -and $gameProcess.HasExited -and $null -eq $gameExitCode) {
        $gameExitCode = $gameProcess.ExitCode
    }

    try {
        $remainingProcesses = @(Get-ProcessPaths)
        foreach ($remaining in $remainingProcesses) {
            try {
                Stop-Process -Id ([int]$remaining.ProcessId) -Force
            }
            catch {
                $restoreErrors += "Could not stop process $($remaining.ProcessId): $($_.Exception.Message)"
            }
        }
    }
    catch {
        $restoreErrors += "Could not inspect Hela processes during cleanup: $($_.Exception.Message)"
    }

    if ($watcherProcess -and -not $watcherProcess.HasExited) {
        try {
            if (-not $watcherProcess.WaitForExit(15000)) {
                Stop-Process -Id $watcherProcess.Id -Force
            }
        }
        catch {
            $restoreErrors += "Could not stop the TCP monitor: $($_.Exception.Message)"
        }
    }

    if ($snapshotsReady) {
        if ($stdoutTask) {
            try {
                [System.IO.File]::WriteAllText(
                    (Join-Path $RunDirectory 'stdout-sanitized.txt'),
                    (Get-SanitizedLogText $stdoutTask.Result),
                    [System.Text.UTF8Encoding]::new($false))
            }
            catch {
                $restoreErrors += "Could not save sanitized stdout: $($_.Exception.Message)"
            }
        }
        if ($stderrTask) {
            try {
                [System.IO.File]::WriteAllText(
                    (Join-Path $RunDirectory 'stderr-sanitized.txt'),
                    (Get-SanitizedLogText $stderrTask.Result),
                    [System.Text.UTF8Encoding]::new($false))
            }
            catch {
                $restoreErrors += "Could not save sanitized stderr: $($_.Exception.Message)"
            }
        }

        $runLogPath = Join-Path $RunDirectory 'd3d12on11.log'
        if ([System.IO.File]::Exists($gameLog) -and
            -not [System.IO.File]::Exists($runLogPath)) {
            try {
                Copy-Item -LiteralPath $gameLog -Destination $runLogPath
            }
            catch {
                $restoreErrors += "Could not preserve test proxy log: $($_.Exception.Message)"
            }
        }
        foreach ($name in $externalReadbackNames) {
            $source = Join-Path $externalReadbackRoot $name
            $destination = Join-Path $RunDirectory $name
            if ([System.IO.File]::Exists($source) -and
                -not [System.IO.File]::Exists($destination)) {
                try {
                    Copy-Item -LiteralPath $source -Destination $destination
                    $readbackFiles += [pscustomobject]@{
                        name = $name
                        length = (Get-Item -LiteralPath $destination).Length
                        sha256 = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
                    }
                }
                catch {
                    $restoreErrors += "Could not preserve test readback $name`: $($_.Exception.Message)"
                }
            }
        }

        $savedLog = Join-Path $preState 'd3d12on11.log'
        if ($originalLogExists -and [System.IO.File]::Exists($savedLog)) {
            try {
                Copy-Item -LiteralPath $savedLog -Destination $gameLog -Force
            }
            catch {
                $restoreErrors += "Could not restore proxy log: $($_.Exception.Message)"
            }
        }
        elseif (-not $originalLogExists -and [System.IO.File]::Exists($gameLog)) {
            try {
                Remove-Item -LiteralPath $gameLog -Force
            }
            catch {
                $restoreErrors += "Could not remove test proxy log: $($_.Exception.Message)"
            }
        }

        foreach ($name in $externalReadbackNames) {
            $destination = Join-Path $externalReadbackRoot $name
            $saved = Join-Path (Join-Path $preState 'external-readbacks') $name
            try {
                if ([System.IO.File]::Exists($saved)) {
                    Copy-Item -LiteralPath $saved -Destination $destination -Force
                }
                elseif ([System.IO.File]::Exists($destination)) {
                    Remove-Item -LiteralPath $destination -Force
                }
            }
            catch {
                $restoreErrors += "Could not restore readback $name`: $($_.Exception.Message)"
            }
        }
    }
}

$originalLogRestored = if ($originalLogExists) {
    $savedLog = Join-Path $preState 'd3d12on11.log'
    [System.IO.File]::Exists($gameLog) -and
        [System.IO.File]::Exists($savedLog) -and
        (Get-FileHash -LiteralPath $gameLog -Algorithm SHA256).Hash -eq
        (Get-FileHash -LiteralPath $savedLog -Algorithm SHA256).Hash
}
else {
    -not [System.IO.File]::Exists($gameLog)
}

$readbacksRestored = $true
foreach ($name in $externalReadbackNames) {
    $destination = Join-Path $externalReadbackRoot $name
    $saved = Join-Path (Join-Path $preState 'external-readbacks') $name
    if ([System.IO.File]::Exists($saved)) {
        if (-not [System.IO.File]::Exists($destination) -or
            (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath $saved -Algorithm SHA256).Hash) {
            $readbacksRestored = $false
        }
    }
    elseif ([System.IO.File]::Exists($destination)) {
        $readbacksRestored = $false
    }
}

$dllsUnchanged = $true
foreach ($dllName in $expectedDllHashes.Keys) {
    $dllPath = Join-Path $gameBin $dllName
    if (-not [System.IO.File]::Exists($dllPath) -or
        (Get-FileHash -LiteralPath $dllPath -Algorithm SHA256).Hash -ne $dllHashesBefore[$dllName]) {
        $dllsUnchanged = $false
    }
}
$sharedOverridesUnchanged = (ConvertTo-Json -InputObject @(Get-OverrideManifest $sharedOverrides) -Compress) -eq
    (ConvertTo-Json -InputObject $sharedOverrideManifest -Compress)
$gameProcessesRunning = $true
try {
    $gameProcessesRunning = (Get-ProcessPaths).Count -gt 0
}
catch {
    $restoreErrors += "Could not verify final game process state: $($_.Exception.Message)"
}
$resultStatus = if ($runError) {
    'failed'
}
elseif ($targetReadbackReady) {
    'readback-captured'
}
elseif ($timeoutReached) {
    'timeout-before-target-readback'
}
else {
    'process-exited-before-target-readback'
}
$networkSummary = Get-HelaNetworkObservationSummary -Path $networkLog
$firewallVerdict = Get-HelaFirewallVerdict `
    -ConfigurationVerified ($blockExitCode -eq 0 -and $null -ne $blockVerification) `
    -NetworkSummary $networkSummary
$firewallDetail = [ordered]@{
    verdict = $firewallVerdict
    configurationVerification = $blockVerification
    trafficObservation = $networkSummary
    limitation = 'No TCP connection observed is not proof of zero egress. The monitor is process-scoped, polling-based, and does not observe UDP or payload; Established indicates a TCP handshake, not payload delivery or remote acceptance.'
}
$result = [ordered]@{
    runId = [System.IO.Path]::GetFileName($RunDirectory)
    gpu = 'GTX 1050 Ti'
    status = $resultStatus
    startedAt = if ($gameProcess) { $gameProcess.StartTime.ToString('o') } else { $null }
    targetPresent = $TargetPresent
    timeoutSeconds = $TimeoutSeconds
    timeoutReached = $timeoutReached
    successfulPresentCalls = $presentCount
    postReadbackObservation = $menuObservation
    targetReadbackLogged = $targetReadbackLogged
    targetReadbackSourceSaved = [System.IO.File]::Exists((Join-Path $RunDirectory "HelaReadback_Present$($TargetPresent)_source.bmp"))
    targetReadbackBackbufferSaved = [System.IO.File]::Exists((Join-Path $RunDirectory "HelaReadback_Present$($TargetPresent)_backbuffer.bmp"))
    readbackFiles = $readbackFiles
    gameExitCode = $gameExitCode
    gameRootDllsUnchanged = $dllsUnchanged
    sharedOverridesUnchanged = $sharedOverridesUnchanged
    originalProxyLogRestored = $originalLogRestored
    externalReadbacksRestored = $readbacksRestored
    gameProcessesRunning = $gameProcessesRunning
    firewallCheck = $firewallVerdict
    firewall = $firewallDetail
    networkLog = $networkLog
    runError = $runError
    restoreErrors = $restoreErrors
    realGraphicsVisuallyValidated = $false
}
$result | ConvertTo-Json -Depth 7 |
    Set-Content -LiteralPath (Join-Path $RunDirectory 'run-result.json') -Encoding UTF8
$result | ConvertTo-Json -Depth 7

if ($runError -or $restoreErrors.Count -gt 0 -or
    -not $originalLogRestored -or -not $readbacksRestored -or
    -not $dllsUnchanged -or -not $sharedOverridesUnchanged -or
    $gameProcessesRunning) {
    throw 'Run or restoration verification failed; inspect run-result.json and restoreErrors.'
}
}
catch {
    $errorRecord = $_
    $diagnostic = [ordered]@{
        schema = 'hela-powershell-error/v1'
        timestampUtc = [System.DateTime]::UtcNow.ToString('o')
        stage = $diagnosticStage
        message = $errorRecord.Exception.Message
        exceptionType = $errorRecord.Exception.GetType().FullName
        fullyQualifiedErrorId = $errorRecord.FullyQualifiedErrorId
        category = [string]$errorRecord.CategoryInfo
        scriptStackTrace = $errorRecord.ScriptStackTrace
        positionMessage = if ($errorRecord.InvocationInfo) { $errorRecord.InvocationInfo.PositionMessage } else { $null }
    }
    try {
        if ([System.IO.Directory]::Exists($RunDirectory)) {
            $diagnostic | ConvertTo-Json -Depth 6 |
                Set-Content -LiteralPath $diagnosticPath -Encoding UTF8
        }
    }
    catch {
        [Console]::Error.WriteLine("Could not save PowerShell diagnostic '$diagnosticPath': $($_.Exception.Message)")
    }
    [Console]::Error.WriteLine("Hela runner failed at stage '$diagnosticStage'. See '$diagnosticPath'. $($errorRecord.Exception.Message)")
    throw
}
