# ==============================================================================
# EmbeddedPixel - Development Environment & Toolchain Audit Script
# ==============================================================================

Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host "         EmbeddedPixel Development Environment Audit            " -ForegroundColor Cyan
Write-Host "=================================================================`n" -ForegroundColor Cyan

function Check-Tool {
    param (
        [string]$Name,
        [string]$CommandName,
        [string[]]$FallbackPaths = @(),
        [string]$InstallHint = ""
    )

    $cmd = Get-Command $CommandName -ErrorAction SilentlyContinue
    $resolvedPath = $null

    if ($cmd) {
        $resolvedPath = $cmd.Source
    } else {
        foreach ($p in $FallbackPaths) {
            if (Test-Path $p) {
                $resolvedPath = $p
                break
            }
        }
    }

    if ($resolvedPath) {
        $ver = ""
        try {
            $fInfo = Get-Item $resolvedPath -ErrorAction SilentlyContinue
            if ($fInfo -and $fInfo.VersionInfo -and $fInfo.VersionInfo.ProductVersion) {
                $ver = "v$($fInfo.VersionInfo.ProductVersion)"
            } else {
                $rawOut = & $resolvedPath --version 2>&1 | Select-Object -First 1
                $ver = $rawOut.ToString().Trim()
            }
        } catch {
            $ver = "Installed"
        }
        Write-Host "  [+] $Name" -ForegroundColor Green -NoNewline
        Write-Host " -> $ver" -ForegroundColor Gray
        Write-Host "      Location: $resolvedPath" -ForegroundColor DarkGray
        return $true
    } else {
        Write-Host "  [-] $Name" -ForegroundColor Red -NoNewline
        Write-Host " -> NOT FOUND" -ForegroundColor Yellow
        if ($InstallHint) {
            Write-Host "      Hint: $InstallHint" -ForegroundColor DarkYellow
        }
        return $false
    }
}

Write-Host "[1/5] Core Build & Toolchain" -ForegroundColor White
Check-Tool "ARM GCC (arm-none-eabi-gcc)" "arm-none-eabi-gcc" @(
    "$env:USERPROFILE\.gemini\arm-gnu-toolchain\arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi\bin\arm-none-eabi-gcc.exe"
) "Run: powershell -ExecutionPolicy Bypass -File .\scripts\setup_toolchain.ps1"

Check-Tool "CMake" "cmake" @(
    "C:\Program Files\CMake\bin\cmake.exe"
) "Run: winget install -e --id Kitware.CMake"

Check-Tool "Ninja Build" "ninja" @(
    "C:\Program Files\Ninja\ninja.exe",
    "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe\ninja.exe"
) "Run: winget install -e --id Ninja-build.Ninja"

Write-Host "`n[2/5] Flashing & Hardware Debugging" -ForegroundColor White
$cubeProgPaths = @(
    "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
    "C:\Program Files (x86)\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
    "D:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
    "C:\ST\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
)
Check-Tool "STM32CubeProgrammer CLI" "STM32_Programmer_CLI" $cubeProgPaths "Download from: https://www.st.com/en/development-tools/stm32cubeprog.html"

Write-Host "`n[3/5] Host Scripting & Automation" -ForegroundColor White
Check-Tool "Python 3" "python" @(
    "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe"
) "Run: winget install -e --id Python.Python.3.12"

Check-Tool "Git" "git" @(
    "C:\Program Files\Git\cmd\git.exe"
) "Run: winget install -e --id Git.Git"

Check-Tool "Node.js" "node" @(
    "C:\Program Files\nodejs\node.exe"
) "Run: winget install -e --id OpenJS.NodeJS"

Write-Host "`n[4/5] Diagnostics & Serial Terminals" -ForegroundColor White
Check-Tool "Wireshark" "wireshark" @(
    "C:\Program Files\Wireshark\Wireshark.exe"
) "Run: winget install -e --id WiresharkFoundation.Wireshark"

Check-Tool "PuTTY" "putty" @(
    "C:\Program Files\PuTTY\putty.exe",
    "C:\Program Files (x86)\PuTTY\putty.exe"
) "Run: winget install -e --id PuTTY.PuTTY"

Write-Host "`n[5/5] Connected Hardware Probes" -ForegroundColor White
try {
    $ports = Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue
    if ($ports) {
        foreach ($port in $ports) {
            Write-Host "  [+] Serial Port: $($port.DeviceID) - $($port.Description)" -ForegroundColor Green
            Write-Host "      PNP ID: $($port.PNPDeviceID)" -ForegroundColor DarkGray
        }
    } else {
        Write-Host "  [!] No active serial COM ports detected." -ForegroundColor Yellow
    }
} catch {
    Write-Host "  [!] Unable to query COM ports." -ForegroundColor Yellow
}

Write-Host "`n=================================================================" -ForegroundColor Cyan
Write-Host "                        Audit Complete                           " -ForegroundColor Cyan
Write-Host "=================================================================`n" -ForegroundColor Cyan
