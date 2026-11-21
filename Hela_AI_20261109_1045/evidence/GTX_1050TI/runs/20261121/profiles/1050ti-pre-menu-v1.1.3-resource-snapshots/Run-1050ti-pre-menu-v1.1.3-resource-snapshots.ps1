param(
    [Parameter(Mandatory = $true)]
    [switch]$AllowOnlineRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ProfileRoot = $PSScriptRoot
$BaseRunner = 'D:\Plugin_ChatGpt\GHIDRA MCP\Hela-Export\Repositories\Hela_test\Hela_AI_20261109_1045\evidence\GTX_1050TI\runs\20261119\profiles\1050ti-pre-menu-v1.1.2-historical-env\Run-1050ti-pre-menu-v1.1.2-historical-env.ps1'
$ManifestPath = Join-Path $ProfileRoot 'run-manifest.json'
$PackagePath = Join-Path $ProfileRoot 'package'

& $BaseRunner -AllowOnlineRun:$AllowOnlineRun `
    -PackagePath $PackagePath `
    -ManifestPath $ManifestPath
