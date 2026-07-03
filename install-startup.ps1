$ErrorActionPreference = "Stop"

$appName = "Codex Tray Gauge"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$exePath = Join-Path $scriptDir "codex-tray-gauge.exe"

if (-not (Test-Path -LiteralPath $exePath)) {
    throw "Cannot find codex-tray-gauge.exe in: $scriptDir"
}

$startupDir = [Environment]::GetFolderPath("Startup")
$shortcutPath = Join-Path $startupDir "$appName.lnk"

$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = $exePath
$shortcut.WorkingDirectory = $scriptDir
$shortcut.Description = "Start Codex Tray Gauge when Windows starts"
$shortcut.IconLocation = "$exePath,0"
$shortcut.Save()

Write-Host "Startup enabled:"
Write-Host $shortcutPath
