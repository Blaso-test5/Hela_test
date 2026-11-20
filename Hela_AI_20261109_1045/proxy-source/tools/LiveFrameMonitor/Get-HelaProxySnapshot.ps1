<#
.SYNOPSIS
    Read-only diagnostic snapshot of a Hela proxy installation (PowerShell 5.1).

.DESCRIPTION
    Computes SHA-256 of d3d12.dll, dxgi.dll and the override *.cso files and, optionally,
    compares them with a hela-run-manifest/v1 (-RunManifestPath) or a restore point
    (-RestorePointPath, manifest schema hela-proxy-restore/v1, modern or legacy).

    The tool NEVER installs, restores, deletes or writes inside the game directory or the
    restore point. The only file it can create is the optional -JsonOut report, which must
    live outside the game directory and outside the restore point.

    Per-item statuses:
      Match          live state equals what the reference records (including "recorded as absent, is absent")
      Mismatch       file exists but hash/size differs, or a directory sits where a file is expected
      Missing        the reference expects it, it is not on disk
      Extra          on disk, but the reference records it as absent / does not list it
      NotAttestable  the comparison cannot be decided (no hash recorded, unreadable path,
                     overrides directory name not supplied, ...). Never reported as a match.
      Observed       inventory mode only (no reference)
      NotInspected   inventory mode only (overrides directory name not supplied)

    Verdict and exit code:
      0  Match or InventoryOnly   1  Mismatch (Mismatch/Missing/Extra present)
      2  Invalid input / error    3  Inconclusive (no discrepancy found but something is NotAttestable)

.PARAMETER GameDirectory
    Directory that contains d3d12.dll and dxgi.dll. Required for inventory and run-manifest
    modes. Optional with -RestorePointPath: falls back to game_directory of the restore manifest.

.PARAMETER RunManifestPath
    Path to a hela-run-manifest/v1 JSON file.

.PARAMETER RestorePointPath
    Path to a restore manifest (hela-proxy-restore/v1) or to a restore point directory that
    contains one (restore-manifest.json, manifest.json, or exactly one *.json with that schema).

.PARAMETER OverridesDirectoryName
    Relative directory (inside GameDirectory) that holds the override *.cso files. There is NO
    default: without it overrides cannot be located and are reported as NotAttestable.

.PARAMETER JsonOut
    Optional destination for the JSON report. Must not exist, its parent must exist, and it must
    be outside the game directory and outside the restore point.

.EXAMPLE
    .\Get-HelaProxySnapshot.ps1 -GameDirectory 'D:\Game\Win64' -OverridesDirectoryName shader_overrides

.EXAMPLE
    .\Get-HelaProxySnapshot.ps1 -GameDirectory 'D:\Game\Win64' -RunManifestPath .\run.json -OverridesDirectoryName shader_overrides -JsonOut C:\Temp\snap.json
#>
#Requires -Version 5.1
[CmdletBinding(DefaultParameterSetName = 'Inventory')]
param(
    [Parameter(ParameterSetName = 'Inventory', Mandatory = $true)]
    [Parameter(ParameterSetName = 'Run', Mandatory = $true)]
    [Parameter(ParameterSetName = 'Restore')]
    [string]$GameDirectory,

    [Parameter(ParameterSetName = 'Run', Mandatory = $true)]
    [string]$RunManifestPath,

    [Parameter(ParameterSetName = 'Restore', Mandatory = $true)]
    [string]$RestorePointPath,

    [string]$OverridesDirectoryName,

    [string]$JsonOut
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$script:ToolName = 'Get-HelaProxySnapshot'
$script:ToolVersion = '1.0.0'
$script:RuntimeFiles = @('d3d12.dll', 'dxgi.dll')
$script:Sha256Pattern = '^[0-9a-fA-F]{64}$'

# ----------------------------------------------------------------------------------------------
# Generic helpers
# ----------------------------------------------------------------------------------------------

function Test-IsObject {
    param($Value)
    return ($Value -is [System.Management.Automation.PSCustomObject])
}

function Get-JsonProp {
    param($Obj, [string]$Name)
    $p = $Obj.PSObject.Properties[$Name]
    if ($null -eq $p) { return [pscustomobject]@{ Exists = $false; Value = $null } }
    return [pscustomobject]@{ Exists = $true; Value = $p.Value }
}

function Assert-Sha256 {
    param($Value, [string]$What)
    if ($Value -isnot [string] -or $Value -notmatch $script:Sha256Pattern) {
        throw "$What must be a 64-character hexadecimal SHA-256 string."
    }
    return $Value.ToLowerInvariant()
}

function Resolve-FullPath {
    param([string]$Path, [string]$What)
    if ([string]::IsNullOrWhiteSpace($Path)) { throw "$What is empty." }
    try {
        $u = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
        $full = [System.IO.Path]::GetFullPath($u)
    } catch {
        throw "$What is not a valid path ('$Path'): $($_.Exception.Message)"
    }
    $root = [System.IO.Path]::GetPathRoot($full)
    if ($full.Length -gt $root.Length) { $full = $full.TrimEnd('\', '/') }
    return $full
}

function Test-PathInside {
    param([string]$Path, [string]$Root)
    $cmp = [System.StringComparison]::OrdinalIgnoreCase
    if ($Path.Equals($Root, $cmp)) { return $true }
    $rootSep = $Root
    if (-not ($rootSep.EndsWith('\') -or $rootSep.EndsWith('/'))) { $rootSep = $Root + '\' }
    return $Path.StartsWith($rootSep, $cmp)
}

function ConvertTo-SafeRelativePath {
    param([string]$Path, [string]$What)
    if ([string]::IsNullOrWhiteSpace($Path)) { throw "$What is empty." }
    if ($Path -ne $Path.Trim()) { throw "$What has leading or trailing whitespace: '$Path'." }
    if ([System.IO.Path]::IsPathRooted($Path) -or $Path.StartsWith('\') -or $Path.StartsWith('/')) {
        throw "$What must be a relative path: '$Path'."
    }
    if ($Path.Contains(':')) { throw "$What must not contain ':' (drive or stream syntax): '$Path'." }
    $invalid = [System.IO.Path]::GetInvalidFileNameChars()
    $segments = @($Path -split '[\\/]')
    foreach ($s in $segments) {
        if ($s.Length -eq 0) { throw "$What contains an empty path segment: '$Path'." }
        if ($s -eq '.' -or $s -eq '..') { throw "$What must not contain '.' or '..' segments: '$Path'." }
        if ($s.TrimEnd('.', ' ').Length -ne $s.Length) { throw "$What has a segment ending with '.' or space: '$Path'." }
        if ($s.IndexOfAny($invalid) -ge 0) { throw "$What contains invalid characters: '$Path'." }
        if ($s -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\..*)?$') { throw "$What contains a reserved device name: '$Path'." }
    }
    return ($segments -join '\')
}

function Get-ContainedPath {
    param([string]$Root, [string]$Relative)
    $full = [System.IO.Path]::GetFullPath([System.IO.Path]::Combine($Root, $Relative))
    if (-not (Test-PathInside -Path $full -Root $Root)) {
        throw "Path '$Relative' resolves outside of '$Root'."
    }
    return $full
}

function Read-JsonObject {
    param([string]$Path)
    if (-not [System.IO.File]::Exists($Path)) { throw "File not found: $Path" }
    try {
        $text = [System.IO.File]::ReadAllText($Path)
    } catch {
        throw "Cannot read '$Path': $($_.Exception.Message)"
    }
    try {
        $obj = $text | ConvertFrom-Json
    } catch {
        throw "Invalid JSON in '$Path': $($_.Exception.Message)"
    }
    if (-not (Test-IsObject $obj)) { throw "'$Path' must contain a JSON object at top level." }
    return $obj
}

# ----------------------------------------------------------------------------------------------
# Live file inspection (strictly read-only)
# ----------------------------------------------------------------------------------------------

function Get-FileState {
    param([string]$Path)
    $r = [ordered]@{ state = 'Absent'; sha256 = $null; bytes = $null; error = $null }
    try {
        if ([System.IO.Directory]::Exists($Path)) { $r.state = 'Directory'; return $r }
        if (-not [System.IO.File]::Exists($Path)) { return $r }
        $share = [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete
        $fs = New-Object System.IO.FileStream($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, $share)
        try {
            $sha = [System.Security.Cryptography.SHA256]::Create()
            try { $hash = $sha.ComputeHash($fs) } finally { $sha.Dispose() }
            $r.bytes = $fs.Length
            $r.sha256 = (($hash | ForEach-Object { $_.ToString('x2') }) -join '')
            $r.state = 'File'
        } finally {
            $fs.Dispose()
        }
    } catch {
        $r.state = 'Error'
        $r.sha256 = $null
        $r.bytes = $null
        $r.error = $_.Exception.Message
    }
    return $r
}

function ConvertTo-ActualState {
    param($State)
    $kind = 'error'
    if ($State.state -eq 'File') { $kind = 'file' }
    elseif ($State.state -eq 'Absent') { $kind = 'absent' }
    elseif ($State.state -eq 'Directory') { $kind = 'directory' }
    return [ordered]@{ kind = $kind; sha256 = $State.sha256; bytes = $State.bytes; error = $State.error }
}

# ----------------------------------------------------------------------------------------------
# Findings
# ----------------------------------------------------------------------------------------------

function New-Finding {
    param([string]$Category, [string]$Name, [string]$Status, [string]$Reason, $Expected, $Actual, $Required = $null)
    return [ordered]@{
        category = $Category
        name     = $Name
        status   = $Status
        required = $Required
        expected = $Expected
        actual   = $Actual
        reason   = $Reason
    }
}

function Compare-FileToExpectation {
    param([string]$Category, [string]$Name, $Exp, $Act, $Required = $null)
    $expected = [ordered]@{ present = $Exp.present; sha256 = $Exp.sha256; bytes = $Exp.bytes }
    $actual = ConvertTo-ActualState $Act
    $status = 'NotAttestable'
    $reason = ''

    if ($Act.state -eq 'Error') {
        $reason = 'Live path could not be inspected: ' + $Act.error
    } elseif ($null -eq $Exp.present) {
        $reason = 'The reference has no attestable entry for this item, so no comparison is possible.'
    } elseif ($Exp.present -eq $false) {
        if ($Act.state -eq 'Absent') {
            $status = 'Match'
            $reason = 'Absent, as recorded by the reference.'
        } else {
            $status = 'Extra'
            $reason = 'Present on disk, but the reference records it as absent.'
        }
    } else {
        switch ($Act.state) {
            'Absent' {
                $status = 'Missing'
                $reason = 'Recorded as present by the reference, but not found on disk.'
            }
            'Directory' {
                $status = 'Mismatch'
                $reason = 'A directory exists where the reference expects a file.'
            }
            'File' {
                if ($null -eq $Exp.sha256) {
                    $reason = 'The file exists, but the reference does not record its hash.'
                } else {
                    $hashOk = [string]::Equals($Act.sha256, $Exp.sha256, [System.StringComparison]::OrdinalIgnoreCase)
                    $bytesOk = ($null -eq $Exp.bytes) -or ($Act.bytes -eq $Exp.bytes)
                    if ($hashOk -and $bytesOk) {
                        $status = 'Match'
                        $reason = 'SHA-256 matches the reference.'
                    } elseif (-not $hashOk) {
                        $status = 'Mismatch'
                        $reason = 'SHA-256 differs from the reference.'
                    } else {
                        $status = 'Mismatch'
                        $reason = 'File size differs from the reference.'
                    }
                }
            }
        }
    }
    return (New-Finding -Category $Category -Name $Name -Status $status -Reason $reason -Expected $expected -Actual $actual -Required $Required)
}

function New-ObservedFinding {
    param([string]$Category, [string]$Name, $Act)
    $reason = 'Observed (inventory mode, no reference to compare).'
    return (New-Finding -Category $Category -Name $Name -Status 'Observed' -Reason $reason -Expected $null -Actual (ConvertTo-ActualState $Act))
}

# ----------------------------------------------------------------------------------------------
# Reference manifests
# ----------------------------------------------------------------------------------------------

function ConvertFrom-RunManifest {
    param($M, [string]$Path)
    $ctx = "Run manifest '$Path'"

    $schema = Get-JsonProp $M 'schema'
    if (-not $schema.Exists -or $schema.Value -isnot [string] -or $schema.Value -cne 'hela-run-manifest/v1') {
        throw "$ctx`: schema must be 'hela-run-manifest/v1'."
    }
    $att = Get-JsonProp $M 'runtimeAttestation'
    if (-not $att.Exists -or $att.Value -isnot [string] -or $att.Value -cne 'module-and-shader-sha256') {
        throw "$ctx`: runtimeAttestation must be 'module-and-shader-sha256'."
    }
    $testVersion = $null
    $tv = Get-JsonProp $M 'testVersion'
    if ($tv.Exists) {
        if ($tv.Value -isnot [string] -or [string]::IsNullOrWhiteSpace($tv.Value)) { throw "$ctx`: testVersion must be a non-empty string." }
        $testVersion = $tv.Value
    }

    $rt = Get-JsonProp $M 'runtime'
    if (-not $rt.Exists -or -not (Test-IsObject $rt.Value)) { throw "$ctx`: runtime must be an object with d3d12 and dxgi hashes." }
    $runtime = [ordered]@{}
    foreach ($n in @('d3d12', 'dxgi')) {
        $p = Get-JsonProp $rt.Value $n
        if (-not $p.Exists) { throw "$ctx`: runtime.$n is required." }
        $h = Assert-Sha256 $p.Value "$ctx`: runtime.$n"
        $runtime["$n.dll"] = @{ present = $true; sha256 = $h; bytes = $null; basis = 'run-manifest' }
    }

    $ov = Get-JsonProp $M 'overrides'
    if (-not $ov.Exists -or -not (Test-IsObject $ov.Value)) { throw "$ctx`: overrides must be an object mapping relative .cso path to SHA-256." }
    $seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $entries = New-Object System.Collections.Generic.List[object]
    foreach ($prop in $ov.Value.PSObject.Properties) {
        $rel = ConvertTo-SafeRelativePath $prop.Name "$ctx`: overrides key"
        if (-not $rel.EndsWith('.cso', [System.StringComparison]::OrdinalIgnoreCase)) { throw "$ctx`: overrides key '$($prop.Name)' must end with .cso." }
        if (-not $seen.Add($rel)) { throw "$ctx`: duplicate overrides key '$rel'." }
        $v = $prop.Value
        $sha = $null
        $bytes = $null
        if ($v -is [string]) {
            $sha = Assert-Sha256 $v "$ctx`: overrides['$rel']"
        } elseif (Test-IsObject $v) {
            $s = Get-JsonProp $v 'sha256'
            if (-not $s.Exists) { throw "$ctx`: overrides['$rel'].sha256 is required." }
            $sha = Assert-Sha256 $s.Value "$ctx`: overrides['$rel'].sha256"
            $b = Get-JsonProp $v 'bytes'
            if ($b.Exists -and $null -ne $b.Value) {
                if (($b.Value -isnot [int] -and $b.Value -isnot [long]) -or $b.Value -lt 0) { throw "$ctx`: overrides['$rel'].bytes must be a non-negative integer." }
                $bytes = $b.Value
            }
        } else {
            throw "$ctx`: overrides['$rel'] must be a SHA-256 string or an object with sha256."
        }
        $entries.Add([pscustomobject]@{ path = $rel; sha256 = $sha; bytes = $bytes; required = $false })
    }

    $req = Get-JsonProp $M 'requiredOverrides'
    if (-not $req.Exists -or $req.Value -isnot [System.Array]) { throw "$ctx`: requiredOverrides must be an array." }
    foreach ($r in $req.Value) {
        if ($r -isnot [string]) { throw "$ctx`: requiredOverrides entries must be strings." }
        $rel = ConvertTo-SafeRelativePath $r "$ctx`: requiredOverrides entry"
        $hit = $null
        foreach ($e in $entries) { if ($e.path -ieq $rel) { $hit = $e } }
        if ($null -eq $hit) { throw "$ctx`: requiredOverrides entry '$rel' is not listed in overrides." }
        $hit.required = $true
    }

    $notes = New-Object System.Collections.Generic.List[string]
    $notes.Add('A run manifest does not state whether the overrides directory exists; only the set of .cso files is compared.')
    return @{
        kind                  = 'run-manifest'
        path                  = $Path
        schema                = 'hela-run-manifest/v1'
        format                = 'run-manifest'
        testVersion           = $testVersion
        createdUtc            = $null
        runtimeAttestation    = 'module-and-shader-sha256'
        gameDirectoryRecorded = $null
        runtime               = $runtime
        overridesDir          = @{ expectedPresent = $null; basis = 'run-manifest-does-not-specify' }
        overrides             = $entries
        notes                 = $notes
    }
}

function ConvertFrom-RestoreManifest {
    param($M, [string]$Path)
    $ctx = "Restore manifest '$Path'"

    $schema = Get-JsonProp $M 'schema'
    if (-not $schema.Exists -or $schema.Value -isnot [string] -or $schema.Value -cne 'hela-proxy-restore/v1') {
        throw "$ctx`: schema must be 'hela-proxy-restore/v1'."
    }
    $recorded = $null
    $gd = Get-JsonProp $M 'game_directory'
    if ($gd.Exists -and $null -ne $gd.Value) {
        if ($gd.Value -isnot [string] -or [string]::IsNullOrWhiteSpace($gd.Value)) { throw "$ctx`: game_directory must be a non-empty string." }
        $recorded = $gd.Value
    }
    $created = $null
    $cu = Get-JsonProp $M 'created_utc'
    if ($cu.Exists -and $null -ne $cu.Value) {
        if ($cu.Value -isnot [string]) { throw "$ctx`: created_utc must be a string." }
        $created = $cu.Value
    }

    $rt = Get-JsonProp $M 'runtime'
    if (-not $rt.Exists -or -not (Test-IsObject $rt.Value)) { throw "$ctx`: runtime must be an object." }

    $notes = New-Object System.Collections.Generic.List[string]
    $runtime = [ordered]@{}
    $anyObj = $false
    $anyStr = $false
    foreach ($name in $script:RuntimeFiles) {
        $p = Get-JsonProp $rt.Value $name
        $v = $p.Value
        if (-not $p.Exists -or $null -eq $v) {
            $runtime[$name] = @{ present = $null; sha256 = $null; bytes = $null; basis = 'manifest-has-no-entry' }
            $notes.Add("runtime '$name' has no entry in the restore manifest: its original state is not attestable.")
        } elseif ($v -is [string]) {
            # Legacy: a string means "file was present, this is its hash".
            $anyStr = $true
            $runtime[$name] = @{ present = $true; sha256 = (Assert-Sha256 $v "$ctx`: runtime['$name']"); bytes = $null; basis = 'legacy-string' }
        } elseif (Test-IsObject $v) {
            $anyObj = $true
            $pp = Get-JsonProp $v 'present'
            if (-not $pp.Exists -or $pp.Value -isnot [bool]) { throw "$ctx`: runtime['$name'].present must be a boolean." }
            $sp = Get-JsonProp $v 'sha256'
            $hasSha = ($sp.Exists -and $null -ne $sp.Value)
            if ($pp.Value) {
                $sha = $null
                if ($hasSha) {
                    $sha = Assert-Sha256 $sp.Value "$ctx`: runtime['$name'].sha256"
                } else {
                    $notes.Add("runtime '$name' was recorded as present without a hash: its content is not attestable.")
                }
                $runtime[$name] = @{ present = $true; sha256 = $sha; bytes = $null; basis = 'modern-object' }
            } else {
                if ($hasSha) { throw "$ctx`: runtime['$name'] has present=false but also a sha256." }
                $runtime[$name] = @{ present = $false; sha256 = $null; bytes = $null; basis = 'modern-object' }
            }
        } else {
            throw "$ctx`: runtime['$name'] must be a hash string (legacy) or an object {present, sha256}."
        }
    }

    $df = Get-JsonProp $M 'overrides_directory_present'
    $dirExpected = $true
    $dirBasis = 'legacy-assumed'
    if ($df.Exists) {
        if ($df.Value -isnot [bool]) { throw "$ctx`: overrides_directory_present must be a boolean." }
        $dirExpected = $df.Value
        $dirBasis = 'manifest'
    } else {
        $notes.Add('overrides_directory_present is missing: legacy semantics assume the overrides directory was present. This is an assumption; absence is never inferred from legacy data.')
    }

    $ovArr = Get-JsonProp $M 'overrides'
    if (-not $ovArr.Exists -or $ovArr.Value -isnot [System.Array]) { throw "$ctx`: overrides must be an array." }
    $seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $entries = New-Object System.Collections.Generic.List[object]
    foreach ($o in $ovArr.Value) {
        if (-not (Test-IsObject $o)) { throw "$ctx`: overrides entries must be objects {path, bytes, sha256}." }
        $pp = Get-JsonProp $o 'path'
        if (-not $pp.Exists -or $pp.Value -isnot [string]) { throw "$ctx`: overrides entry without a string path." }
        $rel = ConvertTo-SafeRelativePath $pp.Value "$ctx`: overrides path"
        if (-not $rel.EndsWith('.cso', [System.StringComparison]::OrdinalIgnoreCase)) { throw "$ctx`: overrides path '$($pp.Value)' must end with .cso." }
        if (-not $seen.Add($rel)) { throw "$ctx`: duplicate overrides path '$rel'." }
        $bp = Get-JsonProp $o 'bytes'
        if (-not $bp.Exists -or ($bp.Value -isnot [int] -and $bp.Value -isnot [long]) -or $bp.Value -lt 0) { throw "$ctx`: overrides['$rel'].bytes must be a non-negative integer." }
        $sp = Get-JsonProp $o 'sha256'
        if (-not $sp.Exists) { throw "$ctx`: overrides['$rel'].sha256 is required." }
        $entries.Add([pscustomobject]@{ path = $rel; sha256 = (Assert-Sha256 $sp.Value "$ctx`: overrides['$rel'].sha256"); bytes = $bp.Value; required = $null })
    }
    if ($dirExpected -eq $false -and $entries.Count -gt 0) {
        throw "$ctx`: overrides_directory_present is false but overrides lists files."
    }

    $modernSignal = ($anyObj -or $df.Exists)
    if (-not $modernSignal) {
        $format = 'restore-legacy'
        $notes.Add('Legacy restore manifest: runtime files are recorded as present; the original absence of a runtime file or of the overrides directory cannot be attested.')
    } elseif ($anyStr) {
        $format = 'restore-mixed'
        $notes.Add('Restore manifest mixes legacy and modern fields; each field is interpreted with its own semantics.')
    } else {
        $format = 'restore-modern'
    }

    return @{
        kind                  = 'restore-point'
        path                  = $Path
        schema                = 'hela-proxy-restore/v1'
        format                = $format
        testVersion           = $null
        createdUtc            = $created
        runtimeAttestation    = $null
        gameDirectoryRecorded = $recorded
        runtime               = $runtime
        overridesDir          = @{ expectedPresent = $dirExpected; basis = $dirBasis }
        overrides             = $entries
        notes                 = $notes
    }
}

function Resolve-RestoreManifestPath {
    param([string]$Full)
    if ([System.IO.File]::Exists($Full)) {
        return @{ manifest = $Full; dir = [System.IO.Path]::GetDirectoryName($Full) }
    }
    if (-not [System.IO.Directory]::Exists($Full)) { throw "RestorePointPath not found: $Full" }
    foreach ($n in @('restore-manifest.json', 'manifest.json')) {
        $c = [System.IO.Path]::Combine($Full, $n)
        if ([System.IO.File]::Exists($c)) { return @{ manifest = $c; dir = $Full } }
    }
    $hits = New-Object System.Collections.Generic.List[string]
    foreach ($f in [System.IO.Directory]::GetFiles($Full, '*.json')) {
        try {
            $o = Read-JsonObject $f
            $s = Get-JsonProp $o 'schema'
            if ($s.Exists -and $s.Value -is [string] -and $s.Value -ceq 'hela-proxy-restore/v1') { $hits.Add($f) }
        } catch { }
    }
    if ($hits.Count -eq 1) { return @{ manifest = $hits[0]; dir = $Full } }
    if ($hits.Count -eq 0) { throw "No hela-proxy-restore/v1 manifest found in restore point directory: $Full" }
    throw "Ambiguous restore point: $($hits.Count) restore manifests found in $Full. Pass the manifest file explicitly."
}

# ----------------------------------------------------------------------------------------------
# -JsonOut validation / writing
# ----------------------------------------------------------------------------------------------

function Resolve-JsonOutPath {
    param([string]$Raw, [string]$GameFull, $ProtectedDirs, $ProtectedFiles)
    if ($Raw.EndsWith('\') -or $Raw.EndsWith('/')) { throw "-JsonOut must be a file path, not a directory: '$Raw'." }
    $full = Resolve-FullPath $Raw '-JsonOut'
    if (Test-PathInside -Path $full -Root $GameFull) { throw "-JsonOut must be outside the game directory: '$full'." }
    foreach ($d in $ProtectedDirs) {
        if (Test-PathInside -Path $full -Root $d) { throw "-JsonOut must be outside the restore point ('$d'): '$full'." }
    }
    foreach ($f in $ProtectedFiles) {
        if ($full.Equals($f, [System.StringComparison]::OrdinalIgnoreCase)) { throw "-JsonOut must not be the reference manifest itself." }
    }
    $parent = [System.IO.Path]::GetDirectoryName($full)
    if ([string]::IsNullOrEmpty($parent) -or -not [System.IO.Directory]::Exists($parent)) { throw "-JsonOut parent directory does not exist: '$parent'." }
    if ([System.IO.File]::Exists($full) -or [System.IO.Directory]::Exists($full)) { throw "-JsonOut already exists; refusing to overwrite: '$full'." }
    return $full
}

function Write-NewTextFile {
    param([string]$Path, [string]$Text)
    $fs = New-Object System.IO.FileStream($Path, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
    try {
        $enc = New-Object System.Text.UTF8Encoding($false)
        $bytes = $enc.GetBytes($Text)
        $fs.Write($bytes, 0, $bytes.Length)
    } finally {
        $fs.Dispose()
    }
}

# ----------------------------------------------------------------------------------------------
# Overrides inspection
# ----------------------------------------------------------------------------------------------

function Add-OverrideFindings {
    param($List, [string]$GameFull, [string]$OvName, $Ref, [string]$Mode)
    $inventory = ($Mode -eq 'Inventory')

    if ([string]::IsNullOrEmpty($OvName)) {
        if ($inventory) {
            $List.Add((New-Finding -Category 'overrides' -Name '(set)' -Status 'NotInspected' -Reason 'OverridesDirectoryName was not provided; override files were not inspected.' -Expected $null -Actual $null))
        } else {
            $List.Add((New-Finding -Category 'overrides' -Name '(set)' -Status 'NotAttestable' -Reason 'OverridesDirectoryName was not provided: the overrides directory cannot be located, so override files and the overrides set are not attestable.' -Expected $null -Actual $null))
        }
        return
    }

    $dirFull = Get-ContainedPath -Root $GameFull -Relative $OvName
    $dirExists = [System.IO.Directory]::Exists($dirFull)
    $blocked = ((-not $dirExists) -and [System.IO.File]::Exists($dirFull))
    $dirKind = 'absent'
    if ($dirExists) { $dirKind = 'directory' } elseif ($blocked) { $dirKind = 'file' }
    $dirActual = [ordered]@{ kind = $dirKind; sha256 = $null; bytes = $null; error = $null }

    if ($inventory) {
        $List.Add((New-Finding -Category 'overridesDirectory' -Name $OvName -Status 'Observed' -Reason 'Observed (inventory mode, no reference to compare).' -Expected $null -Actual $dirActual))
    } elseif ($null -ne $Ref.overridesDir.expectedPresent) {
        $expPresent = $Ref.overridesDir.expectedPresent
        $basis = $Ref.overridesDir.basis
        $suffix = ''
        if ($basis -eq 'legacy-assumed') { $suffix = ' (legacy manifest: presence is assumed, not attested)' }
        $status = 'NotAttestable'
        $reason = ''
        if ($blocked) {
            $reason = 'A file, not a directory, exists at the overrides directory path.'
        } elseif ($expPresent) {
            if ($dirExists) { $status = 'Match'; $reason = 'Overrides directory present, as expected' + $suffix + '.' }
            else { $status = 'Missing'; $reason = 'Overrides directory expected but not found' + $suffix + '.' }
        } else {
            if ($dirExists) { $status = 'Extra'; $reason = 'Overrides directory exists, but the restore manifest records it as originally absent.' }
            else { $status = 'Match'; $reason = 'Overrides directory absent, as recorded by the restore manifest.' }
        }
        $List.Add((New-Finding -Category 'overridesDirectory' -Name $OvName -Status $status -Reason $reason -Expected ([ordered]@{ present = $expPresent; sha256 = $null; bytes = $null }) -Actual $dirActual))
    }

    if ($blocked) {
        $List.Add((New-Finding -Category 'overrides' -Name '(set)' -Status 'NotAttestable' -Reason 'The overrides path is a file, so its content set cannot be enumerated.' -Expected $null -Actual $dirActual))
        return
    }

    $liveFiles = New-Object System.Collections.Generic.List[string]
    if ($dirExists) {
        try {
            $all = [System.IO.Directory]::GetFiles($dirFull, '*.cso', [System.IO.SearchOption]::AllDirectories)
            foreach ($f in $all) {
                if ($f.EndsWith('.cso', [System.StringComparison]::OrdinalIgnoreCase)) { $liveFiles.Add($f) }
            }
            $liveFiles.Sort()
        } catch {
            $List.Add((New-Finding -Category 'overrides' -Name '(set)' -Status 'NotAttestable' -Reason ('The overrides directory could not be enumerated: ' + $_.Exception.Message) -Expected $null -Actual $dirActual))
            return
        }
    }

    $expKeys = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    if (-not $inventory) {
        foreach ($o in ($Ref.overrides | Sort-Object -Property path)) {
            $full = Get-ContainedPath -Root $dirFull -Relative $o.path
            [void]$expKeys.Add($o.path)
            $st = Get-FileState $full
            $exp = @{ present = $true; sha256 = $o.sha256; bytes = $o.bytes }
            $List.Add((Compare-FileToExpectation -Category 'override' -Name $o.path -Exp $exp -Act $st -Required $o.required))
        }
    }
    foreach ($f in $liveFiles) {
        $rel = $f.Substring($dirFull.Length).TrimStart('\', '/')
        if ($expKeys.Contains($rel)) { continue }
        $st = Get-FileState $f
        if ($inventory) {
            $List.Add((New-ObservedFinding -Category 'override' -Name $rel -Act $st))
        } else {
            $List.Add((New-Finding -Category 'override' -Name $rel -Status 'Extra' -Reason 'Present in the overrides directory but not listed by the reference.' -Expected ([ordered]@{ present = $false; sha256 = $null; bytes = $null }) -Actual (ConvertTo-ActualState $st)))
        }
    }
}

# ----------------------------------------------------------------------------------------------
# Output
# ----------------------------------------------------------------------------------------------

function Get-StatusColor {
    param([string]$Status)
    switch ($Status) {
        'Match' { return 'Green' }
        'Mismatch' { return 'Red' }
        'Missing' { return 'Red' }
        'Extra' { return 'Red' }
        'NotAttestable' { return 'Yellow' }
        default { return 'Gray' }
    }
}

function Write-Summary {
    param($Result)
    Write-Host ''
    Write-Host 'Hela Proxy Snapshot (read-only)'
    Write-Host ('  Mode           : ' + $Result.mode)
    Write-Host ('  Game directory : ' + $Result.gameDirectory.path + '  [source: ' + $Result.gameDirectory.source + ']')
    if ($null -ne $Result.reference) {
        Write-Host ('  Reference      : ' + $Result.reference.path)
        Write-Host ('  Format         : ' + $Result.reference.format + '  (' + $Result.reference.schema + ')')
    }
    foreach ($n in $Result.notes) { Write-Host ('  Note           : ' + $n) -ForegroundColor DarkGray }
    Write-Host ''
    foreach ($f in $Result.items) {
        $sha = ''
        if ($null -ne $f.actual -and $null -ne $f.actual.sha256) { $sha = ' sha256=' + $f.actual.sha256.Substring(0, 16) + '...' }
        $req = ''
        if ($f.required -eq $true) { $req = ' [required]' }
        $line = '  {0,-13} {1}/{2}{3}{4}' -f $f.status, $f.category, $f.name, $req, $sha
        Write-Host $line -ForegroundColor (Get-StatusColor $f.status)
        Write-Host ('                  ' + $f.reason) -ForegroundColor DarkGray
    }
    Write-Host ''
    $c = $Result.summary.counts
    Write-Host ('  Counts: Match={0} Mismatch={1} Missing={2} Extra={3} NotAttestable={4} Observed={5} NotInspected={6}' -f $c.Match, $c.Mismatch, $c.Missing, $c.Extra, $c.NotAttestable, $c.Observed, $c.NotInspected)
    $color = 'Yellow'
    if ($Result.summary.verdict -eq 'Match' -or $Result.summary.verdict -eq 'InventoryOnly') { $color = 'Green' }
    elseif ($Result.summary.verdict -eq 'Mismatch') { $color = 'Red' }
    Write-Host ('  Verdict: ' + $Result.summary.verdict + '  (exit code ' + $Result.summary.exitCode + ')') -ForegroundColor $color
    Write-Host ''
}

# ----------------------------------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------------------------------

$exitCode = 2
try {
    $mode = 'Inventory'
    if ($PSCmdlet.ParameterSetName -eq 'Run') { $mode = 'RunManifest' }
    elseif ($PSCmdlet.ParameterSetName -eq 'Restore') { $mode = 'RestorePoint' }

    $ovName = $null
    if ($PSBoundParameters.ContainsKey('OverridesDirectoryName')) {
        $ovName = ConvertTo-SafeRelativePath $OverridesDirectoryName '-OverridesDirectoryName'
    }

    $ref = $null
    $protectedDirs = New-Object System.Collections.Generic.List[string]
    $protectedFiles = New-Object System.Collections.Generic.List[string]
    if ($mode -eq 'RunManifest') {
        $mp = Resolve-FullPath $RunManifestPath '-RunManifestPath'
        $ref = ConvertFrom-RunManifest (Read-JsonObject $mp) $mp
        $protectedFiles.Add($mp)
    } elseif ($mode -eq 'RestorePoint') {
        $rp = Resolve-FullPath $RestorePointPath '-RestorePointPath'
        $loc = Resolve-RestoreManifestPath $rp
        $ref = ConvertFrom-RestoreManifest (Read-JsonObject $loc.manifest) $loc.manifest
        $protectedDirs.Add($loc.dir)
        $protectedFiles.Add($loc.manifest)
    }

    $notes = New-Object System.Collections.Generic.List[string]
    if ($null -ne $ref) { $notes.AddRange($ref.notes) }

    $gameSource = 'parameter'
    $gameRaw = $null
    if ($PSBoundParameters.ContainsKey('GameDirectory')) {
        $gameRaw = $GameDirectory
    } elseif ($mode -eq 'RestorePoint') {
        if ($null -eq $ref.gameDirectoryRecorded) { throw 'GameDirectory was not provided and the restore manifest has no game_directory.' }
        if (-not [System.IO.Path]::IsPathRooted($ref.gameDirectoryRecorded)) { throw "game_directory in the restore manifest is not an absolute path: '$($ref.gameDirectoryRecorded)'." }
        $gameRaw = $ref.gameDirectoryRecorded
        $gameSource = 'restore-manifest'
    }
    $gameFull = Resolve-FullPath $gameRaw '-GameDirectory'
    if (-not [System.IO.Directory]::Exists($gameFull)) { throw "Game directory does not exist: $gameFull" }
    if ($mode -eq 'RestorePoint' -and $gameSource -eq 'parameter' -and $null -ne $ref.gameDirectoryRecorded) {
        $recFull = $null
        try { $recFull = Resolve-FullPath $ref.gameDirectoryRecorded 'game_directory' } catch { }
        if ($null -ne $recFull -and -not $recFull.Equals($gameFull, [System.StringComparison]::OrdinalIgnoreCase)) {
            $notes.Add("GameDirectory differs from game_directory recorded in the restore manifest ('$($ref.gameDirectoryRecorded)'); the parameter was used.")
        }
    }

    $jsonOutFull = $null
    if ($PSBoundParameters.ContainsKey('JsonOut')) {
        $jsonOutFull = Resolve-JsonOutPath -Raw $JsonOut -GameFull $gameFull -ProtectedDirs $protectedDirs -ProtectedFiles $protectedFiles
    }

    # --- inspection -------------------------------------------------------------------------
    $findings = New-Object System.Collections.Generic.List[object]
    foreach ($name in $script:RuntimeFiles) {
        $st = Get-FileState ([System.IO.Path]::Combine($gameFull, $name))
        if ($mode -eq 'Inventory') {
            $findings.Add((New-ObservedFinding -Category 'runtime' -Name $name -Act $st))
        } else {
            $findings.Add((Compare-FileToExpectation -Category 'runtime' -Name $name -Exp $ref.runtime[$name] -Act $st))
        }
    }
    Add-OverrideFindings -List $findings -GameFull $gameFull -OvName $ovName -Ref $ref -Mode $mode

    # --- summary ----------------------------------------------------------------------------
    $counts = [ordered]@{ Match = 0; Mismatch = 0; Missing = 0; Extra = 0; NotAttestable = 0; Observed = 0; NotInspected = 0 }
    foreach ($f in $findings) { $counts[$f.status] = $counts[$f.status] + 1 }
    if ($mode -eq 'Inventory') {
        $verdict = 'InventoryOnly'; $exitCode = 0
    } elseif (($counts.Mismatch + $counts.Missing + $counts.Extra) -gt 0) {
        $verdict = 'Mismatch'; $exitCode = 1
    } elseif ($counts.NotAttestable -gt 0) {
        $verdict = 'Inconclusive'; $exitCode = 3
    } else {
        $verdict = 'Match'; $exitCode = 0
    }

    $refInfo = $null
    if ($null -ne $ref) {
        $refInfo = [ordered]@{
            kind                  = $ref.kind
            path                  = $ref.path
            schema                = $ref.schema
            format                = $ref.format
            testVersion           = $ref.testVersion
            createdUtc            = $ref.createdUtc
            runtimeAttestation    = $ref.runtimeAttestation
            gameDirectoryRecorded = $ref.gameDirectoryRecorded
        }
    }
    $ovInfo = [ordered]@{ name = $ovName; path = $null; present = $null; inspected = $false }
    if ($null -ne $ovName) {
        $ovPath = Get-ContainedPath -Root $gameFull -Relative $ovName
        $ovInfo = [ordered]@{ name = $ovName; path = $ovPath; present = [System.IO.Directory]::Exists($ovPath); inspected = $true }
    }

    $result = [ordered]@{
        schema             = 'hela-proxy-snapshot/v1'
        generatedUtc       = [DateTime]::UtcNow.ToString('o')
        tool               = [ordered]@{ name = $script:ToolName; version = $script:ToolVersion; readOnly = $true }
        mode               = $mode
        gameDirectory      = [ordered]@{ path = $gameFull; source = $gameSource; exists = $true }
        reference          = $refInfo
        overridesDirectory = $ovInfo
        notes              = $notes.ToArray()
        items              = $findings.ToArray()
        summary            = [ordered]@{ verdict = $verdict; exitCode = $exitCode; counts = $counts }
    }

    Write-Summary $result
    $json = $result | ConvertTo-Json -Depth 10
    Write-Output $json

    if ($null -ne $jsonOutFull) {
        Write-NewTextFile -Path $jsonOutFull -Text $json
        Write-Host ('JSON report written to: ' + $jsonOutFull)
    }
} catch {
    $exitCode = 2
    Write-Error -Message ("$($script:ToolName): " + $_.Exception.Message) -ErrorAction Continue
}
exit $exitCode
