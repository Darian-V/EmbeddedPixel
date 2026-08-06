$url = "https://developer.arm.com/-/media/Files/downloads/gnu/13.3.rel1/binrel/arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi.zip"
$zipPath = "$env:TEMP\arm-toolchain.zip"
$destDir = "$env:USERPROFILE\.gemini\arm-gnu-toolchain"

Write-Host "Downloading ARM GNU Toolchain (this may take a minute)..."
Invoke-WebRequest -Uri $url -OutFile $zipPath

Write-Host "Extracting..."
# Clean up destination if it exists
if (Test-Path $destDir) { Remove-Item -Recurse -Force $destDir }
New-Item -ItemType Directory -Force -Path $destDir | Out-Null
Expand-Archive -Path $zipPath -DestinationPath $destDir -Force

$binPath = "$destDir\arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi\bin"

Write-Host "Adding $binPath to User PATH..."
$userPath = [Environment]::GetEnvironmentVariable("PATH", "User")
if ($userPath -notlike "*$binPath*") {
    [Environment]::SetEnvironmentVariable("PATH", "$userPath;$binPath", "User")
    Write-Host "Successfully added to PATH!"
} else {
    Write-Host "PATH already contains the toolchain bin folder."
}

# Clean up zip
Remove-Item $zipPath

Write-Host "Done! Please restart your terminal/IDE to pick up the new PATH."
