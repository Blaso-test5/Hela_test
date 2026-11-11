[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = "High")]
param(
    [ValidateSet("Inventory", "Prepare", "Run", "Restore")]
    [string]$Action = "Inventory",
    [string]$GameExePath = "D:\SteamLibrary\steamapps\common\Hela Demo\Hela\Binaries\Win64\Hela-Win64-Shipping.exe",
    [string]$VersionPath = "",
    [string]$ManifestPath = "",
    [string]$RunRoot = "",
    [string]$RunDirectory = "",
    [UInt32]$Present = 400,
    [UInt32]$MonitorTimeoutSeconds = 180
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$LiveMonitorLauncher = Join-Path $ScriptDir "Start-HelaLiveMonitor.ps1"
$GameDir = [IO.Path]::GetFullPath((Split-Path -Parent $GameExePath))
$PatchRoot = Join-Path $GameDir "D3D12On11Patch"
$RuntimeDir = Join-Path $PatchRoot "versions"
$InstalledOverridesDir = Join-Path $PatchRoot "overrides"
$LogPath = Join-Path $GameDir "d3d12on11.log"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $ScriptDir))
if ([string]::IsNullOrWhiteSpace($RunRoot)) {
    $RunRoot = Join-Path $ProjectRoot "evidence\GTX_950M\runs"
}

if ($Action -in @("Prepare", "Run") -and $Present -lt 100 -or
    $Action -in @("Prepare", "Run") -and $Present % 100 -ne 0) {
    throw "-Present must be a positive multiple of 100."
}

function Get-Sha256 {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required file not found: $Path"
    }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-DirectoryInventory {
    param([Parameter(Mandatory = $true)][string]$Path)
    $items = @()
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        return $items
    }
    foreach ($file in Get-ChildItem -LiteralPath $Path -File -Recurse -Force) {
        $relativePath = $file.FullName.Substring($Path.TrimEnd('\').Length).TrimStart('\')
        $items += [ordered]@{
            path = $relativePath
            bytes = $file.Length
            sha256 = Get-Sha256 -Path $file.FullName
        }
    }
    return $items
}

function Get-InstalledInventory {
    $runtime = [ordered]@{}
    foreach ($name in @("d3d12.dll", "dxgi.dll")) {
        $path = Join-Path $GameDir $name
        $runtime[$name] = if (Test-Path -LiteralPath $path -PathType Leaf) {
            Get-Sha256 -Path $path
        } else {
            $null
        }
    }
    return [ordered]@{
        game_directory = $GameDir
        runtime = $runtime
        overrides = Get-DirectoryInventory -Path $InstalledOverridesDir
    }
}

function Get-AvailablePackageInventory {
    $packages = @()
    if (-not (Test-Path -LiteralPath $RuntimeDir -PathType Container)) {
        return $packages
    }
    foreach ($directory in Get-ChildItem -LiteralPath $RuntimeDir -Directory -Force) {
        $runtimePath = Join-Path $directory.FullName "runtime"
        if (-not (Test-Path -LiteralPath $runtimePath -PathType Container)) {
            $runtimePath = $directory.FullName
        }
        $runtime = [ordered]@{}
        foreach ($name in @("d3d12.dll", "dxgi.dll")) {
            $path = Join-Path $runtimePath $name
            $runtime[$name] = if (Test-Path -LiteralPath $path -PathType Leaf) {
                Get-Sha256 -Path $path
            } else {
                $null
            }
        }
        $overridesPath = Join-Path $directory.FullName "overrides"
        $packages += [ordered]@{
            name = $directory.Name
            package_path = $directory.FullName
            runtime = $runtime
            override_count = @(Get-DirectoryInventory -Path $overridesPath).Count
            manifest_required_for_run = $true
        }
    }
    return $packages
}

function Read-RunManifest {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Run manifest not found: $Path"
    }
    try {
        $manifest = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    } catch {
        throw "Unable to parse run manifest '$Path': $($_.Exception.Message)"
    }
    if ($manifest.schema -ne "hela-run-manifest/v1" -or
        $manifest.runtimeAttestation -ne "module-and-shader-sha256" -or
        [string]::IsNullOrWhiteSpace([string]$manifest.testVersion) -or
        $null -eq $manifest.runtime -or $null -eq $manifest.overrides -or
        $null -eq $manifest.requiredOverrides) {
        throw "Invalid run manifest: $Path"
    }
    foreach ($role in @("d3d12", "dxgi")) {
        $property = $manifest.runtime.PSObject.Properties[$role]
        if ($null -eq $property -or [string]$property.Value -notmatch '^[0-9a-fA-F]{64}$') {
            throw "Manifest is missing a valid runtime.$role SHA-256."
        }
    }
    foreach ($property in $manifest.overrides.PSObject.Properties) {
        if ($property.Name -notmatch '^[^\\/:*?"<>|]+\.cso$' -or
            [string]$property.Value -notmatch '^[0-9a-fA-F]{64}$') {
            throw "Invalid override entry in manifest: $($property.Name)"
        }
    }
    foreach ($name in @($manifest.requiredOverrides)) {
        if ($null -eq $manifest.overrides.PSObject.Properties[$name]) {
            throw "Required override is not pinned in the manifest: $name"
        }
    }
    return $manifest
}

function Assert-PackageMatchesManifest {
    param(
        [Parameter(Mandatory = $true)]$Manifest,
        [Parameter(Mandatory = $true)][string]$PackagePath
    )
    $package = [IO.Path]::GetFullPath($PackagePath)
    $runtimePath = Join-Path $package "runtime"
    $overridePath = Join-Path $package "overrides"
    if (-not (Test-Path -LiteralPath $runtimePath -PathType Container) -or
        -not (Test-Path -LiteralPath $overridePath -PathType Container)) {
        throw "Package must contain runtime\ and overrides\ directories: $package"
    }
    foreach ($role in @("d3d12", "dxgi")) {
        $path = Join-Path $runtimePath "$role.dll"
        $actual = Get-Sha256 -Path $path
        $expected = ([string]$Manifest.runtime.$role).ToLowerInvariant()
        if ($actual -ne $expected) {
            throw "Package fingerprint mismatch for $role.dll: expected $expected, got $actual"
        }
    }

    $expectedNames = @($Manifest.overrides.PSObject.Properties | ForEach-Object { $_.Name } | Sort-Object)
    $actualFiles = @(Get-ChildItem -LiteralPath $overridePath -File -Recurse -Force)
    $actualNames = @($actualFiles | ForEach-Object { $_.Name } | Sort-Object)
    if (Compare-Object -ReferenceObject $expectedNames -DifferenceObject $actualNames) {
        throw "Package override file set differs from the run manifest."
    }
    foreach ($file in $actualFiles) {
        if ($file.DirectoryName -ine $overridePath) {
            throw "Shader overrides must be flat files directly under: $overridePath"
        }
        $property = $Manifest.overrides.PSObject.Properties[$file.Name]
        if ($null -eq $property) {
            throw "Unpinned shader override in package: $($file.Name)"
        }
        $expected = ([string]$property.Value).ToLowerInvariant()
        $actual = Get-Sha256 -Path $file.FullName
        if ($actual -ne $expected) {
            throw "Package fingerprint mismatch for $($file.Name): expected $expected, got $actual"
        }
    }
    return [ordered]@{
        package_path = $package
        test_version = [string]$Manifest.testVersion
        runtime = [ordered]@{
            d3d12 = [string]$Manifest.runtime.d3d12
            dxgi = [string]$Manifest.runtime.dxgi
        }
        override_count = $actualFiles.Count
    }
}

function Assert-GameStopped {
    if (-not (Test-Path -LiteralPath $GameExePath -PathType Leaf)) {
        throw "Game executable not found: $GameExePath"
    }
    $fullExePath = [IO.Path]::GetFullPath($GameExePath)
    $running = @(Get-Process -Name "Hela-Win64-Shipping" -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -and $_.Path -ieq $fullExePath })
    if ($running.Count -gt 0) {
        throw "Hela is already running (PID $($running[0].Id)); close it before installing a test version."
    }
}

function Clear-DirectoryContents {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
        return
    }
    foreach ($item in Get-ChildItem -LiteralPath $Path -Force) {
        Remove-Item -LiteralPath $item.FullName -Recurse -Force
    }
}

function Copy-DirectoryContents {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )
    if (-not (Test-Path -LiteralPath $Destination -PathType Container)) {
        New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    }
    foreach ($item in Get-ChildItem -LiteralPath $Source -Force) {
        Copy-Item -LiteralPath $item.FullName -Destination $Destination -Recurse -Force
    }
}

function Write-RestoreManifest {
    param([Parameter(Mandatory = $true)][string]$RestoreRoot)
    $runtime = [ordered]@{}
    foreach ($name in @("d3d12.dll", "dxgi.dll")) {
        $path = Join-Path $RestoreRoot $name
        $runtime[$name] = if (Test-Path -LiteralPath $path -PathType Leaf) {
            [ordered]@{
                present = $true
                sha256 = Get-Sha256 -Path $path
            }
        } else {
            [ordered]@{
                present = $false
                sha256 = $null
            }
        }
    }
    $overridesRoot = Join-Path $RestoreRoot "overrides"
    $restoreManifest = [ordered]@{
        schema = "hela-proxy-restore/v1"
        game_directory = $GameDir
        created_utc = [DateTime]::UtcNow.ToString("o")
        runtime = $runtime
        overrides_directory_present = Test-Path -LiteralPath $InstalledOverridesDir -PathType Container
        overrides = Get-DirectoryInventory -Path $overridesRoot
    }
    $restoreManifest | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath (Join-Path $RestoreRoot "restore-manifest.json") -Encoding UTF8
}

function Assert-RestorePoint {
    param([Parameter(Mandatory = $true)][string]$RestoreRoot)
    $manifestPath = Join-Path $RestoreRoot "restore-manifest.json"
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Restore manifest not found: $manifestPath"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.schema -ne "hela-proxy-restore/v1" -or
        [IO.Path]::GetFullPath([string]$manifest.game_directory) -ine $GameDir) {
        throw "Restore point does not belong to this game directory: $RestoreRoot"
    }
    foreach ($name in @("d3d12.dll", "dxgi.dll")) {
        $runtimeEntry = $manifest.runtime.PSObject.Properties[$name].Value
        $expectedPresent = $true
        $expectedHash = [string]$runtimeEntry
        if ($runtimeEntry -isnot [string]) {
            $expectedPresent = [bool]$runtimeEntry.present
            $expectedHash = [string]$runtimeEntry.sha256
        }
        $savedPath = Join-Path $RestoreRoot $name
        $savedPresent = Test-Path -LiteralPath $savedPath -PathType Leaf
        if ($savedPresent -ne $expectedPresent) {
            throw "Restore point presence mismatch for $name."
        }
        if ($expectedPresent -and (Get-Sha256 -Path $savedPath) -ne $expectedHash) {
            throw "Restore point hash mismatch for $name."
        }
    }
    $hasOverridesDirectoryProperty = $null -ne $manifest.PSObject.Properties["overrides_directory_present"]
    $expectedOverridesDirectoryPresent = if ($hasOverridesDirectoryProperty) {
        [bool]$manifest.overrides_directory_present
    } else {
        $true
    }
    $savedOverridesDirectoryPresent = Test-Path -LiteralPath (Join-Path $RestoreRoot "overrides") -PathType Container
    if ($savedOverridesDirectoryPresent -ne $expectedOverridesDirectoryPresent) {
        throw "Restore point override directory presence mismatch."
    }
    $expectedOverrides = @($manifest.overrides | ForEach-Object { $_.path } | Sort-Object)
    $actualOverrides = @(Get-DirectoryInventory -Path (Join-Path $RestoreRoot "overrides") |
        ForEach-Object { $_.path } | Sort-Object)
    if (Compare-Object -ReferenceObject $expectedOverrides -DifferenceObject $actualOverrides) {
        throw "Restore point override file set is incomplete or changed."
    }
    foreach ($entry in @($manifest.overrides)) {
        if ([IO.Path]::IsPathRooted([string]$entry.path) -or
            ([string]$entry.path -split '[\\/]') -contains "..") {
            throw "Invalid relative path in restore point: $($entry.path)"
        }
        $path = Join-Path (Join-Path $RestoreRoot "overrides") $entry.path
        if ((Get-Sha256 -Path $path) -ne [string]$entry.sha256) {
            throw "Restore point override hash mismatch: $($entry.path)"
        }
    }
    return $manifest
}

function Save-InstalledState {
    param([Parameter(Mandatory = $true)][string]$RestoreRoot)
    New-Item -ItemType Directory -Path $RestoreRoot -Force | Out-Null
    foreach ($name in @("d3d12.dll", "dxgi.dll")) {
        $source = Join-Path $GameDir $name
        if (Test-Path -LiteralPath $source -PathType Leaf) {
            Copy-Item -LiteralPath $source -Destination $RestoreRoot -Force
        }
    }
    if (Test-Path -LiteralPath $InstalledOverridesDir -PathType Container) {
        Copy-DirectoryContents -Source $InstalledOverridesDir `
            -Destination (Join-Path $RestoreRoot "overrides")
    }
    Write-RestoreManifest -RestoreRoot $RestoreRoot
    $null = Assert-RestorePoint -RestoreRoot $RestoreRoot
}

function Restore-InstalledState {
    param([Parameter(Mandatory = $true)][string]$RestoreRoot)
    Assert-GameStopped
    $restoreManifest = Assert-RestorePoint -RestoreRoot $RestoreRoot
    foreach ($name in @("d3d12.dll", "dxgi.dll")) {
        $runtimeEntry = $restoreManifest.runtime.PSObject.Properties[$name].Value
        $originallyPresent = if ($runtimeEntry -is [string]) {
            $true
        } else {
            [bool]$runtimeEntry.present
        }
        $savedPath = Join-Path $RestoreRoot $name
        $installedPath = Join-Path $GameDir $name
        if ($originallyPresent) {
            Copy-Item -LiteralPath $savedPath -Destination $GameDir -Force
        } elseif (Test-Path -LiteralPath $installedPath -PathType Leaf) {
            Remove-Item -LiteralPath $installedPath -Force
        }
    }
    $hasOverridesDirectoryProperty = $null -ne $restoreManifest.PSObject.Properties["overrides_directory_present"]
    $originalOverridesDirectoryPresent = if ($hasOverridesDirectoryProperty) {
        [bool]$restoreManifest.overrides_directory_present
    } else {
        $true
    }
    if ($originalOverridesDirectoryPresent) {
        Clear-DirectoryContents -Path $InstalledOverridesDir
        Copy-DirectoryContents -Source (Join-Path $RestoreRoot "overrides") -Destination $InstalledOverridesDir
    } elseif (Test-Path -LiteralPath $InstalledOverridesDir -PathType Container) {
        Remove-Item -LiteralPath $InstalledOverridesDir -Recurse -Force
    }

    foreach ($name in @("d3d12.dll", "dxgi.dll")) {
        $runtimeEntry = $restoreManifest.runtime.PSObject.Properties[$name].Value
        $originallyPresent = if ($runtimeEntry -is [string]) {
            $true
        } else {
            [bool]$runtimeEntry.present
        }
        $installedPath = Join-Path $GameDir $name
        $installedPresent = Test-Path -LiteralPath $installedPath -PathType Leaf
        if ($installedPresent -ne $originallyPresent) {
            throw "Restored runtime presence verification failed for $name."
        }
        if ($originallyPresent) {
            $expectedHash = if ($runtimeEntry -is [string]) {
                $runtimeEntry
            } else {
                [string]$runtimeEntry.sha256
            }
            if ((Get-Sha256 -Path $installedPath) -ne $expectedHash) {
                throw "Restored runtime verification failed for $name."
            }
        }
    }
    $expected = @(Get-DirectoryInventory -Path (Join-Path $RestoreRoot "overrides") |
        ForEach-Object { "$($_.path)|$($_.sha256)" } | Sort-Object)
    $actual = @(Get-DirectoryInventory -Path $InstalledOverridesDir |
        ForEach-Object { "$($_.path)|$($_.sha256)" } | Sort-Object)
    if (Compare-Object -ReferenceObject $expected -DifferenceObject $actual) {
        throw "Restored shader override inventory does not match the saved state."
    }
}

function Move-IfPresent {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Destination
    )
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        $parent = Split-Path -Parent $Destination
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
        Move-Item -LiteralPath $Path -Destination $Destination
        return $true
    }
    return $false
}

function Get-RunCapturePaths {
    param([Parameter(Mandatory = $true)][UInt32]$TargetPresent)
    $paths = @()
    foreach ($number in @(100, $TargetPresent | Select-Object -Unique)) {
        foreach ($kind in @("source", "backbuffer")) {
            $paths += "D:\HelaReadback_Present${number}_${kind}.bmp"
        }
    }
    return @($paths | Select-Object -Unique)
}

function Assert-MonitorReport {
    param(
        [Parameter(Mandatory = $true)][string]$ReportPath,
        [Parameter(Mandatory = $true)][UInt32]$TargetPresent,
        [Parameter(Mandatory = $true)][string]$ExpectedVersion
    )
    if (-not (Test-Path -LiteralPath $ReportPath -PathType Leaf)) {
        throw "Live monitor did not produce its report: $ReportPath"
    }
    $report = Get-Content -LiteralPath $ReportPath -Raw | ConvertFrom-Json
    if ($report.run_identity.test_version -ne $ExpectedVersion -or
        $report.run_identity.attestation_status -ne "verified") {
        throw "Live monitor runtime attestation was not verified for $ExpectedVersion."
    }
    if ([UInt32]$report.capture_target.present -ne $TargetPresent -or
        -not $report.capture_target.complete) {
        throw "Live monitor did not complete source/backbuffer readbacks for Present #$TargetPresent."
    }
    foreach ($kind in @("source", "backbuffer")) {
        if ($null -eq $report.bmp_verification.PSObject.Properties[$kind]) {
            throw "Live monitor did not verify the Present #$TargetPresent $kind BMP."
        }
    }
    return $report
}

switch ($Action) {
    "Inventory" {
        [ordered]@{
            installed = Get-InstalledInventory
            available_packages = Get-AvailablePackageInventory
            game_running = (@(Get-Process -Name "Hela-Win64-Shipping" -ErrorAction SilentlyContinue |
                Where-Object { $_.Path -and $_.Path -ieq [IO.Path]::GetFullPath($GameExePath) }).Count -gt 0)
            run_root = [IO.Path]::GetFullPath($RunRoot)
        } | ConvertTo-Json -Depth 8
        return
    }
    "Prepare" {
        if ([string]::IsNullOrWhiteSpace($VersionPath) -or [string]::IsNullOrWhiteSpace($ManifestPath)) {
            throw "-VersionPath and -ManifestPath are required for Prepare."
        }
        $manifest = Read-RunManifest -Path $ManifestPath
        $packageInfo = Assert-PackageMatchesManifest -Manifest $manifest -PackagePath $VersionPath
        [ordered]@{
            ready = $true
            package = $packageInfo
            installed = Get-InstalledInventory
            present = $Present
            mutation_performed = $false
        } | ConvertTo-Json -Depth 8
        return
    }
    "Restore" {
        if ([string]::IsNullOrWhiteSpace($RunDirectory)) {
            throw "-RunDirectory is required for Restore."
        }
        $restoreRoot = Join-Path ([IO.Path]::GetFullPath($RunDirectory)) "restore-point"
        if ($PSCmdlet.ShouldProcess($GameDir, "restore exact DLL and shader override snapshot")) {
            Restore-InstalledState -RestoreRoot $restoreRoot
        }
        return
    }
    "Run" {
        if ([string]::IsNullOrWhiteSpace($VersionPath) -or [string]::IsNullOrWhiteSpace($ManifestPath)) {
            throw "-VersionPath and -ManifestPath are required for Run."
        }
        if ($MonitorTimeoutSeconds -eq 0) {
            throw "-MonitorTimeoutSeconds must be positive so incomplete runs terminate and restore safely."
        }
        if (-not (Test-Path -LiteralPath $LiveMonitorLauncher -PathType Leaf)) {
            throw "Live monitor launcher not found: $LiveMonitorLauncher"
        }
        Assert-GameStopped
        $manifest = Read-RunManifest -Path $ManifestPath
        $packageInfo = Assert-PackageMatchesManifest -Manifest $manifest -PackagePath $VersionPath
        $runTime = [DateTime]::UtcNow
        $runDirectoryPath = Join-Path (Join-Path $RunRoot $runTime.ToString("yyyyMMdd")) `
            "$($manifest.testVersion)-$($runTime.ToString('HHmmssfff'))"
        if (Test-Path -LiteralPath $runDirectoryPath) {
            throw "Refusing to overwrite an existing run directory: $runDirectoryPath"
        }
        if (-not $PSCmdlet.ShouldProcess($runDirectoryPath, "install, run, attest, archive outputs, and restore previous installation")) {
            return
        }
        New-Item -ItemType Directory -Path $runDirectoryPath -Force | Out-Null
        $restoreRoot = Join-Path $runDirectoryPath "restore-point"
        $previousFilesDir = Join-Path $runDirectoryPath "previous-files"
        $readbackDir = Join-Path $runDirectoryPath "readbacks"
        $reportPath = Join-Path $runDirectoryPath "live-monitor-report.json"
        $runManifestPath = Join-Path $runDirectoryPath "run-manifest.json"
        $runError = $null
        $oldLogMoved = $false
        $launcherStarted = $false
        $installationStarted = $false
        $oldReadbacks = @{}

        $runInfo = [ordered]@{
            schema = "hela-proxy-manager-run/v1"
            run_id = Split-Path -Leaf $runDirectoryPath
            test_version = [string]$manifest.testVersion
            started_utc = [DateTime]::UtcNow.ToString("o")
            package_path = $packageInfo.package_path
            target_present = $Present
            status = "preparing"
        }
        $runInfo | ConvertTo-Json -Depth 6 |
            Set-Content -LiteralPath (Join-Path $runDirectoryPath "manager-run.json") -Encoding UTF8

        try {
            Save-InstalledState -RestoreRoot $restoreRoot
            Copy-Item -LiteralPath $ManifestPath -Destination $runManifestPath
            $oldLogMoved = Move-IfPresent -Path $LogPath `
                -Destination (Join-Path $previousFilesDir "d3d12on11.log")
            foreach ($path in Get-RunCapturePaths -TargetPresent $Present) {
                $leaf = Split-Path -Leaf $path
                $oldReadbacks[$path] = Move-IfPresent -Path $path `
                    -Destination (Join-Path $previousFilesDir $leaf)
            }

            $installationStarted = $true
            Copy-Item -LiteralPath (Join-Path $packageInfo.package_path "runtime\d3d12.dll") `
                -Destination $GameDir -Force
            Copy-Item -LiteralPath (Join-Path $packageInfo.package_path "runtime\dxgi.dll") `
                -Destination $GameDir -Force
            Clear-DirectoryContents -Path $InstalledOverridesDir
            Copy-DirectoryContents -Source (Join-Path $packageInfo.package_path "overrides") `
                -Destination $InstalledOverridesDir

            $launcherArgs = @{
                GameExePath = $GameExePath
                LogPath = $LogPath
                JsonOutPath = $reportPath
                RunManifestPath = $runManifestPath
                MonitorMaxWaitSeconds = $MonitorTimeoutSeconds
                SourceBmpPath = "D:\HelaReadback_Present${Present}_source.bmp"
                BackbufferBmpPath = "D:\HelaReadback_Present${Present}_backbuffer.bmp"
                LaunchGame = $true
                EnableReadback = $true
                EnableFullBmpReadback = $true
                FullBmpPresent = $Present
                StopAfterPresent = $Present
                StopGameAfterMonitor = $true
            }
            $launcherStarted = $true
            & $LiveMonitorLauncher @launcherArgs

            $report = Assert-MonitorReport -ReportPath $reportPath `
                -TargetPresent $Present -ExpectedVersion ([string]$manifest.testVersion)
            if ($report.run_identity.attestation_status -ne "verified") {
                throw "Runtime module fingerprints did not verify."
            }
            $runInfo.status = "completed"
        } catch {
            $runError = $_.Exception.Message
            $runInfo.status = "failed"
            $runInfo.error = $runError
        } finally {
            $runInfo.completed_utc = [DateTime]::UtcNow.ToString("o")
            try {
                try {
                    if (($launcherStarted -or $oldLogMoved) -and
                        (Test-Path -LiteralPath $LogPath -PathType Leaf)) {
                        Move-IfPresent -Path $LogPath `
                            -Destination (Join-Path $runDirectoryPath "d3d12on11.log") | Out-Null
                    }
                } catch {
                    $runError = if ($runError) { "$runError; log archive failed: $($_.Exception.Message)" } `
                        else { "Log archive failed: $($_.Exception.Message)" }
                }
                foreach ($path in Get-RunCapturePaths -TargetPresent $Present) {
                    try {
                        if ($launcherStarted -and (Test-Path -LiteralPath $path -PathType Leaf)) {
                            Move-IfPresent -Path $path `
                                -Destination (Join-Path $readbackDir (Split-Path -Leaf $path)) | Out-Null
                        }
                    } catch {
                        $runError = if ($runError) { "$runError; readback archive failed: $($_.Exception.Message)" } `
                            else { "Readback archive failed: $($_.Exception.Message)" }
                    }
                    if ($oldReadbacks.ContainsKey($path) -and $oldReadbacks[$path]) {
                        try {
                            Move-IfPresent -Path (Join-Path $previousFilesDir (Split-Path -Leaf $path)) `
                                -Destination $path | Out-Null
                        } catch {
                            $runError = if ($runError) { "$runError; previous readback restore failed: $($_.Exception.Message)" } `
                                else { "Previous readback restore failed: $($_.Exception.Message)" }
                        }
                    }
                }
                if ($oldLogMoved) {
                    try {
                        Move-IfPresent -Path (Join-Path $previousFilesDir "d3d12on11.log") `
                            -Destination $LogPath | Out-Null
                    } catch {
                        $runError = if ($runError) { "$runError; previous log restore failed: $($_.Exception.Message)" } `
                            else { "Previous log restore failed: $($_.Exception.Message)" }
                    }
                }
            } catch {
                $runError = if ($runError) { "$runError; output archive failed: $($_.Exception.Message)" } `
                    else { "Output archive failed: $($_.Exception.Message)" }
            } finally {
                if ($installationStarted -and (Test-Path -LiteralPath $restoreRoot -PathType Container)) {
                    try {
                        Restore-InstalledState -RestoreRoot $restoreRoot
                        $runInfo.restored = $true
                    } catch {
                        $runInfo.restored = $false
                        $runInfo.status = "restore-failed"
                        $runInfo.restore_error = $_.Exception.Message
                        $runError = if ($runError) { "$runError; restore failed: $($_.Exception.Message)" } `
                            else { "Restore failed: $($_.Exception.Message)" }
                    }
                }
            }

            try {
                if ($runError -and $runInfo.status -eq "completed") {
                    $runInfo.status = "failed"
                }
                $runInfo | ConvertTo-Json -Depth 6 |
                    Set-Content -LiteralPath (Join-Path $runDirectoryPath "manager-run.json") -Encoding UTF8
            } catch {
                $runError = if ($runError) { "$runError; run metadata save failed: $($_.Exception.Message)" } `
                    else { "Run metadata save failed: $($_.Exception.Message)" }
            }
        }

        if ($runError) {
            throw "Proxy test run failed. Outputs and restore point are retained at '$runDirectoryPath'. $runError"
        }
        Write-Output "Run completed and original installation restored: $runDirectoryPath"
        return
    }
}
