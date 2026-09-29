$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildPath = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build'))
if ((Split-Path -Parent $buildPath) -ne $projectRoot) {
    throw 'Refusing to clean outside the project build directory.'
}
if (Test-Path -LiteralPath $buildPath) {
    $directory = Get-Item -LiteralPath $buildPath
    if ($directory.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw 'Refusing to recursively clean a linked build directory.'
    }
    Remove-Item -LiteralPath $buildPath -Recurse -Force
}
