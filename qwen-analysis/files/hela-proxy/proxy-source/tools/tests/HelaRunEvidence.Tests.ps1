$ErrorActionPreference = 'Stop'
$modulePath = Join-Path (Split-Path -Parent $PSScriptRoot) 'HelaRunEvidence.psm1'
Import-Module -Name $modulePath -Force

function Assert-Equal {
    param($Expected, $Actual, [string]$Message)
    if ($Expected -ne $Actual) {
        throw "$Message (expected '$Expected', got '$Actual')"
    }
}

$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('hela-run-evidence-' + [guid]::NewGuid().ToString('N'))
[System.IO.Directory]::CreateDirectory($testRoot) | Out-Null
try {
    $logPath = Join-Path $testRoot 'network-observations.jsonl'
    $events = @(
        [ordered]@{ type = 'monitor-start'; observedUtc = '2026-11-16T21:57:00Z' },
        [ordered]@{ type = 'tcp-state-observed'; processName = 'Hela-Win64-Shipping.exe'; state = 'Established'; remoteAddress = '203.0.113.10'; observedUtc = '2026-11-16T21:58:07Z' },
        [ordered]@{ type = 'monitor-stop'; gameProcessSeen = $true; reason = 'target-processes-exited'; observedUtc = '2026-11-16T21:59:00Z' }
    )
    $events | ForEach-Object {
        ConvertTo-Json -InputObject $_ -Compress -Depth 4
    } | Set-Content -LiteralPath $logPath -Encoding UTF8

    $summary = Get-HelaNetworkObservationSummary -Path $logPath
    Assert-Equal $true $summary.monitorComplete 'Complete watcher log should be marked complete'
    Assert-Equal 'observed-nonloopback-established' (Get-HelaFirewallVerdict -ConfigurationVerified $true -NetworkSummary $summary) 'Established external TCP state should contradict the block claim'
    Assert-Equal 1 $summary.helaEstablishedStateEvents 'Hela established event count should be retained'

    $serialized = ConvertTo-Json -InputObject $summary -Compress
    Assert-Equal $false ($serialized.Contains('203.0.113.10')) 'Summary must not expose remote addresses'

    $events = @(
        [ordered]@{ type = 'monitor-start'; observedUtc = '2026-11-16T21:57:00Z' },
        [ordered]@{ type = 'tcp-state-observed'; processName = 'Hela-Win64-Shipping.exe'; state = 'Established'; remoteAddress = '127.0.0.1'; observedUtc = '2026-11-16T21:58:07Z' },
        [ordered]@{ type = 'tcp-state-observed'; processName = 'Hela-Win64-Shipping.exe'; state = 'Established'; remoteAddress = '192.168.1.10'; observedUtc = '2026-11-16T21:58:08Z' },
        [ordered]@{ type = 'monitor-stop'; gameProcessSeen = $true; reason = 'target-processes-exited'; observedUtc = '2026-11-16T21:59:00Z' }
    )
    $events | ForEach-Object {
        ConvertTo-Json -InputObject $_ -Compress -Depth 4
    } | Set-Content -LiteralPath $logPath -Encoding UTF8
    $summary = Get-HelaNetworkObservationSummary -Path $logPath
    Assert-Equal 1 $summary.loopbackEstablishedStateEvents 'Loopback state should be recorded separately'
    Assert-Equal 1 $summary.helaEstablishedStateEvents 'Non-loopback traffic to a private address still contradicts an all-outbound block'
    Assert-Equal 'observed-nonloopback-established' (Get-HelaFirewallVerdict -ConfigurationVerified $true -NetworkSummary $summary) 'Non-loopback traffic should contradict the all-outbound block'

    $events = @(
        [ordered]@{ type = 'monitor-start'; observedUtc = '2026-11-16T21:57:00Z' },
        [ordered]@{ type = 'tcp-state-observed'; processName = 'crashpad_handler.exe'; state = 'Established'; remoteAddress = '203.0.113.11'; observedUtc = '2026-11-16T21:58:09Z' },
        [ordered]@{ type = 'monitor-stop'; gameProcessSeen = $true; reason = 'target-processes-exited'; observedUtc = '2026-11-16T21:59:00Z' }
    )
    $events | ForEach-Object {
        ConvertTo-Json -InputObject $_ -Compress -Depth 4
    } | Set-Content -LiteralPath $logPath -Encoding UTF8
    $summary = Get-HelaNetworkObservationSummary -Path $logPath
    Assert-Equal 1 $summary.crashpadEstablishedStateEvents 'Crashpad established event should be counted'
    Assert-Equal 'observed-nonloopback-established' (Get-HelaFirewallVerdict -ConfigurationVerified $false -NetworkSummary $summary) 'Observed traffic should remain visible even if configuration verification failed'

    $events = @(
        [ordered]@{ type = 'monitor-start'; observedUtc = '2026-11-16T21:57:00Z' },
        [ordered]@{ type = 'tcp-state-observed'; processName = 'Hela-Win64-Shipping.exe'; state = 'SynSent'; remoteAddress = '203.0.113.10'; observedUtc = '2026-11-16T21:58:07Z' }
    )
    $events | ForEach-Object {
        ConvertTo-Json -InputObject $_ -Compress -Depth 4
    } | Set-Content -LiteralPath $logPath -Encoding UTF8
    $summary = Get-HelaNetworkObservationSummary -Path $logPath
    Assert-Equal 'monitor-incomplete' (Get-HelaFirewallVerdict -ConfigurationVerified $true -NetworkSummary $summary) 'Incomplete watcher log must not be reported as a clean observation'
    Assert-Equal 1 $summary.helaNonLoopbackAttempts 'A connect attempt should be reported separately'

    $summary = Get-HelaNetworkObservationSummary -Path (Join-Path $testRoot 'missing.jsonl')
    Assert-Equal 'configuration-unverified' (Get-HelaFirewallVerdict -ConfigurationVerified $false -NetworkSummary $summary) 'Unverified firewall configuration must not be reported as passed'
    Assert-Equal 'monitor-incomplete' (Get-HelaFirewallVerdict -ConfigurationVerified $true -NetworkSummary $summary) 'A missing watcher file must not be reported as a clean observation'

    $expectedPrograms = @('C:\Hela\Binaries\Win64\Hela-Win64-Shipping.exe', 'C:\Hela\Plugins\Sentry\Binaries\Win64\crashpad_handler.exe')
    $verification = [pscustomobject]@{
        schema = 'hela-egress-verification/1'
        policyStore = 'ActiveStore'
        rules = @(
            [pscustomobject]@{ program = $expectedPrograms[0]; activeRuleFound = $true },
            [pscustomobject]@{ program = $expectedPrograms[1]; activeRuleFound = $true }
        )
    }
    Assert-Equal $true (Test-HelaFirewallVerification -Verification $verification -ExpectedPrograms $expectedPrograms) 'Matching ActiveStore rules should validate'
    $verification.rules[1].program = 'C:\Other\crashpad_handler.exe'
    Assert-Equal $false (Test-HelaFirewallVerification -Verification $verification -ExpectedPrograms $expectedPrograms) 'A rule for the wrong executable must not validate'
}
finally {
    Remove-Module HelaRunEvidence -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $testRoot -Recurse -Force
}

Write-Output 'HelaRunEvidence tests passed.'
