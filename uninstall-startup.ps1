$ErrorActionPreference = "Stop"

$appName = "Codex Tray Gauge"
$startupDir = [Environment]::GetFolderPath("Startup")
$shortcutPath = Join-Path $startupDir "$appName.lnk"

if (Test-Path -LiteralPath $shortcutPath) {
    Remove-Item -LiteralPath $shortcutPath -Force
    Write-Host "Startup disabled:"
    Write-Host $shortcutPath
} else {
    Write-Host "Startup shortcut does not exist:"
    Write-Host $shortcutPath
}
