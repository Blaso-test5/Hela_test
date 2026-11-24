function Test-HelaLoopbackAddress {
    param([AllowNull()][string]$Address)

    $ipAddress = $null
    if ([string]::IsNullOrWhiteSpace($Address) -or
        -not [System.Net.IPAddress]::TryParse($Address, [ref]$ipAddress)) {
        return $false
    }

    if ($ipAddress.IsIPv4MappedToIPv6) {
        $ipAddress = $ipAddress.MapToIPv4()
    }
    return [System.Net.IPAddress]::IsLoopback($ipAddress)
}

function Get-HelaNetworkObservationSummary {
    param([Parameter(Mandatory = $true)][string]$Path)

    $summary = [ordered]@{
        monitorStarted = $false
        monitorStopped = $false
        monitorComplete = $false
        gameProcessSeen = $false
        stopReason = $null
        invalidJsonLines = 0
        helaEstablishedStateEvents = 0
        crashpadEstablishedStateEvents = 0
        helaNonLoopbackAttempts = 0
        otherEstablishedStateEvents = 0
        loopbackEstablishedStateEvents = 0
        firstEstablishedUtc = $null
        lastEstablishedUtc = $null
    }

    if (-not [System.IO.File]::Exists($Path)) {
        return $summary
    }

    foreach ($line in [System.IO.File]::ReadLines($Path)) {
        try {
            $event = ConvertFrom-Json -InputObject $line -ErrorAction Stop
        }
        catch {
            $summary.invalidJsonLines++
            continue
        }

        if ($event.type -eq 'monitor-start') {
            $summary.monitorStarted = $true
            continue
        }
        if ($event.type -eq 'monitor-stop') {
            $summary.monitorStopped = $true
            $summary.stopReason = [string]$event.reason
            $summary.gameProcessSeen = [bool]$event.gameProcessSeen
            continue
        }
        if ($event.type -ne 'tcp-state-observed') {
            continue
        }

        $processName = [string]$event.processName
        $state = [string]$event.state
        $isHela = $processName -ieq 'Hela-Win64-Shipping.exe'
        $isCrashpad = $processName -ieq 'crashpad_handler.exe'
        if (-not $isHela -and -not $isCrashpad) {
            continue
        }

        $isLoopback = Test-HelaLoopbackAddress -Address ([string]$event.remoteAddress)
        if ($state -eq 'Established') {
            if ($isLoopback) {
                $summary.loopbackEstablishedStateEvents++
                continue
            }

            if ($isHela) {
                $summary.helaEstablishedStateEvents++
            }
            else {
                $summary.crashpadEstablishedStateEvents++
            }

            $observedUtc = [string]$event.observedUtc
            if (-not $summary.firstEstablishedUtc -or
                $observedUtc -lt [string]$summary.firstEstablishedUtc) {
                $summary.firstEstablishedUtc = $observedUtc
            }
            if (-not $summary.lastEstablishedUtc -or
                $observedUtc -gt [string]$summary.lastEstablishedUtc) {
                $summary.lastEstablishedUtc = $observedUtc
            }
        }
        elseif ($isHela -and $state -match '^(SynSent|SynReceived)$' -and -not $isLoopback) {
            $summary.helaNonLoopbackAttempts++
        }
    }

    $summary.monitorComplete =
        $summary.monitorStarted -and
        $summary.monitorStopped -and
        $summary.gameProcessSeen -and
        $summary.stopReason -eq 'target-processes-exited' -and
        $summary.invalidJsonLines -eq 0
    return $summary
}

function Get-HelaFirewallVerdict {
    param(
        [Parameter(Mandatory = $true)][bool]$ConfigurationVerified,
        [Parameter(Mandatory = $true)]$NetworkSummary
    )

    if ($NetworkSummary.helaEstablishedStateEvents -gt 0 -or
        $NetworkSummary.crashpadEstablishedStateEvents -gt 0) {
        return 'observed-nonloopback-established'
    }
    if (-not $ConfigurationVerified) {
        return 'configuration-unverified'
    }
    if (-not $NetworkSummary.monitorComplete) {
        return 'monitor-incomplete'
    }
    return 'no-egress-observed'
}

function Test-HelaFirewallVerification {
    param(
        [AllowNull()]$Verification,
        [Parameter(Mandatory = $true)][string[]]$ExpectedPrograms
    )

    if ($null -eq $Verification -or
        $Verification.schema -ne 'hela-egress-verification/1' -or
        $Verification.policyStore -ne 'ActiveStore' -or
        $null -eq $Verification.rules) {
        return $false
    }

    $expected = @(
        $ExpectedPrograms | ForEach-Object { [System.IO.Path]::GetFullPath($_) } | Sort-Object
    )
    $verified = @(
        $Verification.rules |
            Where-Object { $_.activeRuleFound -eq $true } |
            ForEach-Object { [System.IO.Path]::GetFullPath([string]$_.program) } |
            Sort-Object
    )
    if ($verified.Count -ne $expected.Count) {
        return $false
    }
    return $null -eq (Compare-Object -ReferenceObject $expected -DifferenceObject $verified)
}

Export-ModuleMember -Function Test-HelaLoopbackAddress, Get-HelaNetworkObservationSummary, Get-HelaFirewallVerdict, Test-HelaFirewallVerification
