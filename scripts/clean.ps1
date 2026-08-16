# Clean all CMake build directories and intermediate objects while preserving staged programming_files

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

Write-Host "Cleaning build directories in $root..." -ForegroundColor Cyan

$buildDirs = Get-ChildItem -Path $root -Directory | Where-Object { $_.Name -like "build*" -or $_.Name -eq "bin" -or $_.Name -eq "obj" }

foreach ($dir in $buildDirs) {
    Write-Host "Removing $($dir.FullName)..."
    Remove-Item -Recurse -Force $dir.FullName -ErrorAction SilentlyContinue
}

Write-Host "Clean complete! Staged programming_files preserved." -ForegroundColor Green
