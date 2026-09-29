# Kept as a compatible entry point; the right-click menu is now the settings flyout.
param([string]$Executable = (Join-Path $PSScriptRoot '..\build\flyout_test.exe'))
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Executable)) { throw 'Run make tests first.' }
& $Executable
if ($LASTEXITCODE -ne 0) { throw 'Flyout verification failed or was interrupted by a focus change.' }
