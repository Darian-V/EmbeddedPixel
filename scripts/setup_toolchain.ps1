$url = "https://developer.arm.com/-/media/Files/downloads/gnu/13.3.rel1/binrel/arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi.zip"
$zipPath = "$env:TEMP\arm-toolchain.zip"
$destDir = "$env:USERPROFILE\.gemini\arm-gnu-toolchain"
$binPath = "$destDir\arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi\bin"

Write-Host "==> Downloading ARM GNU Toolchain..." -ForegroundColor Cyan
if (Get-Command curl.exe -ErrorAction SilentlyContinue) {
    curl.exe -L $url -o $zipPath --progress-bar
} else {
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest -Uri $url -OutFile $zipPath
}

Write-Host "==> Extracting to $destDir..." -ForegroundColor Cyan
if (Test-Path $destDir) { Remove-Item -Recurse -Force $destDir }
New-Item -ItemType Directory -Force -Path $destDir | Out-Null

if (Get-Command tar.exe -ErrorAction SilentlyContinue) {
    tar.exe -xf $zipPath -C $destDir
} else {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::ExtractToDirectory($zipPath, $destDir)
}

Write-Host "==> Adding to PATH..." -ForegroundColor Cyan
$userPath = [Environment]::GetEnvironmentVariable("PATH", "User")
if ($userPath -notlike "*$binPath*") {
    [Environment]::SetEnvironmentVariable("PATH", "$userPath;$binPath", "User")
    Write-Host "Added $binPath to persistent User PATH." -ForegroundColor Green
} else {
    Write-Host "User PATH already contains $binPath."
}

# Update current PowerShell process PATH
if ($env:PATH -notlike "*$binPath*") {
    $env:PATH = "$binPath;$env:PATH"
}

# Clean up zip
Remove-Item -Force $zipPath -ErrorAction SilentlyContinue

Write-Host "`n[SUCCESS] ARM GNU Toolchain installed! Verifying compiler..." -ForegroundColor Green
if (Test-Path "$binPath\arm-none-eabi-gcc.exe") {
    & "$binPath\arm-none-eabi-gcc.exe" --version
}

