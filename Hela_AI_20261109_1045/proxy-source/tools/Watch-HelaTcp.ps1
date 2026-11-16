[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$GameDirectory,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$OutputPath,

    [ValidateRange(50, 5000)]
    [int]$PollMilliseconds = 250,

    [ValidateRange(1, 86400)]
    [int]$DurationSeconds = 7200,

    [ValidateRange(0, 120)]
    [int]$PostExitSeconds = 5
)

$ErrorActionPreference = 'Stop'

$gameRoot = [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $GameDirectory).Path)
$gameRoot = $gameRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar) +
    [System.IO.Path]::DirectorySeparatorChar
$outputFullPath = [System.IO.Path]::GetFullPath($OutputPath)
if ([System.IO.File]::Exists($outputFullPath)) {
    throw "Output file already exists: $outputFullPath"
}

$outputDirectory = [System.IO.Path]::GetDirectoryName($outputFullPath)
if (-not [System.IO.Directory]::Exists($outputDirectory)) {
    [System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
}

$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
function Write-JsonLine {
    param([Parameter(Mandatory = $true)]$Value)

    $line = ConvertTo-Json -InputObject $Value -Compress -Depth 4
    [System.IO.File]::AppendAllText(
        $outputFullPath,
        $line + [System.Environment]::NewLine,
        $utf8NoBom)
}

Write-JsonLine ([ordered]@{
    type = 'monitor-start'
    observedUtc = [System.DateTime]::UtcNow.ToString('o')
    gameDirectory = $gameRoot
    pollMilliseconds = $PollMilliseconds
    scope = 'TCP connection metadata only; payload and UDP are not captured'
})

$previousConnections = @{}
$gameProcessSeen = $false
$lastTargetProcessUtc = $null
$stopReason = 'duration-expired'
$deadline = [System.DateTime]::UtcNow.AddSeconds($DurationSeconds)

try {
    while ([System.DateTime]::UtcNow -lt $deadline) {
        $processes = @(
            Get-CimInstance -ClassName Win32_Process `
                -Filter "Name='Hela-Win64-Shipping.exe' OR Name='crashpad_handler.exe'" |
                Where-Object {
                    $_.ExecutablePath -and
                    $_.ExecutablePath.StartsWith(
                        $gameRoot,
                        [System.StringComparison]::OrdinalIgnoreCase)
                }
        )

        $processNamesById = @{}
        foreach ($process in $processes) {
            $processId = [int]$process.ProcessId
            $processName = [System.IO.Path]::GetFileName($process.ExecutablePath)
            $processNamesById[$processId] = $processName
            $lastTargetProcessUtc = [System.DateTime]::UtcNow
            if ($processName -ieq 'Hela-Win64-Shipping.exe') {
                $gameProcessSeen = $true
            }
        }

        $currentConnections = @{}
        if ($processNamesById.Count -gt 0) {
            $connections = Get-NetTCPConnection -ErrorAction Stop |
                Where-Object { $processNamesById.ContainsKey([int]$_.OwningProcess) }
            foreach ($connection in $connections) {
                $processId = [int]$connection.OwningProcess
                $key = '{0}|{1}|{2}|{3}|{4}|{5}' -f `
                    $processId,
                    $connection.LocalAddress,
                    $connection.LocalPort,
                    $connection.RemoteAddress,
                    $connection.RemotePort,
                    $connection.State
                $currentConnections[$key] = $true
                if (-not $previousConnections.ContainsKey($key)) {
                    Write-JsonLine ([ordered]@{
                        type = 'tcp-state-observed'
                        observedUtc = [System.DateTime]::UtcNow.ToString('o')
                        processName = $processNamesById[$processId]
                        processId = $processId
                        localAddress = $connection.LocalAddress
                        localPort = $connection.LocalPort
                        remoteAddress = $connection.RemoteAddress
                        remotePort = $connection.RemotePort
                        state = [string]$connection.State
                    })
                }
            }
        }
        $previousConnections = $currentConnections

        if ($gameProcessSeen -and $processes.Count -eq 0 -and $lastTargetProcessUtc) {
            $quietSeconds = ([System.DateTime]::UtcNow - $lastTargetProcessUtc).TotalSeconds
            if ($quietSeconds -ge $PostExitSeconds) {
                $stopReason = 'target-processes-exited'
                break
            }
        }

        Start-Sleep -Milliseconds $PollMilliseconds
    }
}
finally {
    Write-JsonLine ([ordered]@{
        type = 'monitor-stop'
        observedUtc = [System.DateTime]::UtcNow.ToString('o')
        gameProcessSeen = $gameProcessSeen
        reason = $stopReason
    })
}
