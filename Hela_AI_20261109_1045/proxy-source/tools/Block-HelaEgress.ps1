[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$GameDirectory,
    [switch]$Apply,
    [switch]$LaunchGame
)

$ErrorActionPreference = 'Stop'

$gameRoot = [System.IO.Path]::GetFullPath($GameDirectory)
$targets = @(
    [pscustomobject]@{
        DisplayName = 'Hela project - block game outbound'
        Program = Join-Path $gameRoot 'Binaries\Win64\Hela-Win64-Shipping.exe'
    },
    [pscustomobject]@{
        DisplayName = 'Hela project - block Crashpad outbound'
        Program = Join-Path $gameRoot 'Plugins\Sentry\Binaries\Win64\crashpad_handler.exe'
    }
)

foreach ($target in $targets) {
    if (-not [System.IO.File]::Exists($target.Program)) {
        Write-Error "Required executable not found: $($target.Program). Supply the correct -GameDirectory."
        exit 2
    }
}

$profiles = @(Get-NetFirewallProfile -PolicyStore ActiveStore)
foreach ($profileName in @('Domain', 'Private', 'Public')) {
    $profile = $profiles | Where-Object { $_.Name -eq $profileName } | Select-Object -First 1
    if (-not $profile -or $profile.Enabled -ne $true) {
        Write-Error "Windows Firewall profile '$profileName' is missing or disabled; refusing to continue."
        exit 3
    }
}

function Get-NormalizedPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    return [System.IO.Path]::GetFullPath($Path).TrimEnd([char[]]@('\', '/'))
}

function Test-AnyFilterValue {
    param([Parameter(Mandatory = $true)]$Values)

    $items = @($Values | ForEach-Object { [string]$_ })
    return $items.Count -eq 1 -and $items[0] -eq 'Any'
}

function Test-BlockRule {
    param(
        [Parameter(Mandatory = $true)]$Rule,
        [Parameter(Mandatory = $true)][string]$Program
    )

    if ([string]$Rule.Enabled -ne 'True' -or
        [string]$Rule.Direction -ne 'Outbound' -or
        [string]$Rule.Action -ne 'Block') {
        return $false
    }

    $profileText = [string]$Rule.Profile
    if ($profileText -ne 'Any') {
        $profileNames = @($profileText -split '[,\s]+')
        foreach ($requiredProfile in @('Domain', 'Private', 'Public')) {
            if ($profileNames -notcontains $requiredProfile) {
                return $false
            }
        }
    }

    $expectedProgram = Get-NormalizedPath $Program
    $applicationFilters = @(
        Get-NetFirewallApplicationFilter -AssociatedNetFirewallRule $Rule
    )
    $matchingApplication = @(
        $applicationFilters | Where-Object {
            $_.Program -and
            [string]::Equals(
                (Get-NormalizedPath ([string]$_.Program)),
                $expectedProgram,
                [System.StringComparison]::OrdinalIgnoreCase)
        }
    )
    if ($matchingApplication.Count -eq 0) {
        return $false
    }

    $addressFilters = @(Get-NetFirewallAddressFilter -AssociatedNetFirewallRule $Rule)
    if ($addressFilters.Count -ne 1 -or
        -not (Test-AnyFilterValue $addressFilters[0].LocalAddress) -or
        -not (Test-AnyFilterValue $addressFilters[0].RemoteAddress)) {
        return $false
    }

    $portFilters = @(Get-NetFirewallPortFilter -AssociatedNetFirewallRule $Rule)
    if ($portFilters.Count -ne 1) {
        return $false
    }
    $protocol = [string]$portFilters[0].Protocol
    if (($protocol -ne 'Any' -and $protocol -ne '256') -or
        -not (Test-AnyFilterValue $portFilters[0].LocalPort) -or
        -not (Test-AnyFilterValue $portFilters[0].RemotePort)) {
        return $false
    }

    return $true
}

function Find-ValidRule {
    param(
        [Parameter(Mandatory = $true)][string]$PolicyStore,
        [Parameter(Mandatory = $true)]$Target
    )

    $rules = @(
        Get-NetFirewallRule -PolicyStore $PolicyStore `
            -DisplayName $Target.DisplayName -ErrorAction SilentlyContinue
    )
    foreach ($rule in $rules) {
        if (Test-BlockRule -Rule $rule -Program $Target.Program) {
            return $rule
        }
    }
    return $null
}

if ($Apply) {
    $principal = [Security.Principal.WindowsPrincipal]::new(
        [Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Write-Error 'Applying firewall rules requires an elevated PowerShell window; no rules were changed.'
        exit 4
    }

    $profilesWithoutLocalRules = @(
        $profiles | Where-Object { $_.AllowLocalFirewallRules -ne $true }
    )
    if ($profilesWithoutLocalRules.Count -gt 0) {
        Write-Error 'Effective policy disallows local firewall rules; request an administrator/GPO rule instead.'
        exit 5
    }

    foreach ($target in $targets) {
        if (Find-ValidRule -PolicyStore ActiveStore -Target $target) {
            continue
        }

        $namedRules = @(
            Get-NetFirewallRule -PolicyStore PersistentStore `
                -DisplayName $target.DisplayName -ErrorAction SilentlyContinue
        )
        if ($namedRules.Count -gt 0) {
            Write-Error "A persistent rule named '$($target.DisplayName)' exists but does not match the required block; refusing to overwrite it."
            exit 6
        }

        New-NetFirewallRule `
            -PolicyStore PersistentStore `
            -DisplayName $target.DisplayName `
            -Description 'Hela offline graphics test: block all outbound traffic for this executable.' `
            -Direction Outbound `
            -Program $target.Program `
            -Action Block `
            -Profile Any `
            -Protocol Any `
            -LocalAddress Any `
            -RemoteAddress Any `
            -LocalPort Any `
            -RemotePort Any `
            -Enabled True | Out-Null
    }
}

$unverifiedTargets = @()
foreach ($target in $targets) {
    if (-not (Find-ValidRule -PolicyStore ActiveStore -Target $target)) {
        $unverifiedTargets += $target
    }
}

if ($unverifiedTargets.Count -gt 0) {
    $names = ($unverifiedTargets | ForEach-Object { $_.DisplayName }) -join ', '
    Write-Error "No matching effective ActiveStore block rule for: $names. Hela was not launched."
    exit 7
}

foreach ($target in $targets) {
    Write-Output "Verified effective outbound block: $($target.Program)"
}

if ($LaunchGame) {
    Write-Output 'Firewall checks passed; launching Hela directly.'
    Start-Process -FilePath $targets[0].Program
}
