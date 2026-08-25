<#
.SYNOPSIS
    Automated network packet capture and timing analysis for EmbeddedPixel CLI (Port 50002).

.DESCRIPTION
    Configures packet capture on the host network stack for target 169.254.127.150:50002,
    captures the CLI transaction, converts to PCAPNG, and invokes analyze_cli_timing.py.

.PARAMETER TargetIP
    Target firmware IP address (Default: 169.254.127.150)

.PARAMETER TargetPort
    Target TCP port (Default: 50002)

.PARAMETER OutputDir
    Directory to save capture files (Default: current directory / scripts)
#>
param(
    [string]$TargetIP = "169.254.127.150",
    [int]$TargetPort = 50002,
    [string]$OutputDir = "$PSScriptRoot"
)

# Verify Elevation
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Warning "Packet capture requires Administrator privileges."
    Write-Host "Re-launching script in an elevated PowerShell session..." -ForegroundColor Yellow
    Start-Process powershell.exe -Verb RunAs -ArgumentList "-NoExit -ExecutionPolicy Bypass -File `"$PSCommandPath`" -TargetIP $TargetIP -TargetPort $TargetPort -OutputDir `"$OutputDir`""
    exit
}

$etlFile = Join-Path $OutputDir "cli_capture.etl"
$pcapFile = Join-Path $OutputDir "cli_capture.pcapng"

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  EmbeddedPixel CLI Network Packet Capture (Port $TargetPort)" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "Target IP:   $TargetIP"
Write-Host "Target Port: $TargetPort"
Write-Host "ETL Output:  $etlFile"
Write-Host "PCAP Output: $pcapFile"
Write-Host "------------------------------------------------------------"

# Clean up any existing trace/filter
Write-Host "[1/5] Resetting existing capture filters..." -ForegroundColor DarkGray
pktmon filter remove 2>$null
pktmon stop 2>$null
netsh trace stop 2>$null

# Setup pktmon filter
Write-Host "[2/5] Setting up PktMon packet filter for $TargetIP : $TargetPort..." -ForegroundColor Green
pktmon filter add -i $TargetIP -t TCP -p $TargetPort
pktmon filter list

# Start capture
Write-Host "[3/5] Starting packet capture..." -ForegroundColor Green
pktmon start --capture --file-name $etlFile

Write-Host ""
Write-Host ">>> CAPTURE RUNNING <<<" -ForegroundColor Yellow
Write-Host "Execute your CLI command now in EmbeddedPixel Studio (or run test script)." -ForegroundColor Yellow
Write-Host "Press [ENTER] when the command finishes to stop capture..." -ForegroundColor White
$null = Read-Host

# Stop capture
Write-Host "[4/5] Stopping packet capture and removing filters..." -ForegroundColor Green
pktmon stop
pktmon filter remove

# Convert to PCAPNG
Write-Host "[5/5] Converting ETL capture to PCAPNG format..." -ForegroundColor Green
pktmon etl2pcap $etlFile -o $pcapFile

if (Test-Path $pcapFile) {
    Write-Host "`nCapture successfully converted to $pcapFile" -ForegroundColor Green
    Write-Host "`nRunning Timing Breakdown Analysis..." -ForegroundColor Cyan
    python "$PSScriptRoot\analyze_cli_timing.py" "$pcapFile" --ip $TargetIP --port $TargetPort
} else {
    Write-Warning "PCAPNG conversion file not found. Analyzing ETL directly..."
    python "$PSScriptRoot\analyze_cli_timing.py" "$etlFile" --ip $TargetIP --port $TargetPort
}
