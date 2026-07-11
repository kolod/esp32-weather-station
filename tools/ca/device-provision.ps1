<#
.SYNOPSIS
  Build a LittleFS partition image containing the device certificate + key,
  then flash it to the device's 'storage' partition.

.DESCRIPTION
  Uses mklittlefs (from ESP-IDF tools) to create the image and esptool.py to flash.
  Requires the device to be connected via USB.

.EXAMPLE
  .\device-provision.ps1 -Suffix a1b2 -Port COM5
#>
param(
  [Parameter(Mandatory=$true)]
  [string]$Suffix,

  [Parameter(Mandatory=$true)]
  [string]$Port,           # e.g. "COM5"

  [string]$KeyFile   = (Join-Path $PSScriptRoot "device.key"),
  [string]$CertFile  = (Join-Path $PSScriptRoot "device.crt"),
  [string]$IdfPath   = $env:IDF_PATH,
  [string]$TempDir   = (Join-Path $PSScriptRoot "_provision_tmp"),
  [string]$ImageFile = (Join-Path $PSScriptRoot "storage.bin")
)

$ErrorActionPreference = "Stop"

if (!(Test-Path $KeyFile))  { throw "device.key not found. Run device-issue.ps1 first." }
if (!(Test-Path $CertFile)) { throw "device.crt not found. Run device-issue.ps1 first." }

# Build staging directory
if (Test-Path $TempDir) { Remove-Item $TempDir -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $TempDir "certs") | Out-Null
Copy-Item $CertFile (Join-Path $TempDir "certs\device.crt")
Copy-Item $KeyFile  (Join-Path $TempDir "certs\device.key")

# Find mklittlefs in IDF tools
$mklfs = Get-ChildItem -Path (Join-Path $IdfPath "..\..\tools") -Recurse `
         -Filter "mklittlefs*" -File 2>$null | Select-Object -First 1
if (!$mklfs) {
    # Try IDF component manager download location
    $mklfs = Get-ChildItem -Path $IdfPath -Recurse -Filter "mklittlefs*" -File 2>$null |
             Where-Object { $_.Extension -eq '.exe' -or $_.Extension -eq '' } |
             Select-Object -First 1
}
if (!$mklfs) { throw "mklittlefs not found. Ensure ESP-IDF tools are installed." }

Write-Host "Using mklittlefs: $($mklfs.FullName)"

# Storage partition size from partitions.csv: 0x9E0000 = 10354688 bytes
$partSize = 10354688
& "$($mklfs.FullName)" -c $TempDir -s $partSize -p 256 -b 4096 $ImageFile
if ($LASTEXITCODE -ne 0) { throw "mklittlefs failed" }

Write-Host "LittleFS image created: $ImageFile"
Write-Host "Flashing to storage partition at offset 0x620000 on $Port..."

# Storage partition offset from partitions.csv: 0x620000
& python "$IdfPath\components\esptool_py\esptool\esptool.py" `
  --chip esp32 --port $Port --baud 921600 `
  write_flash 0x620000 $ImageFile
if ($LASTEXITCODE -ne 0) { throw "esptool flash failed" }

Write-Host ""
Write-Host "Device provisioned successfully!"
Write-Host "The device at $Port will now serve HTTPS as weather-$Suffix.local"
Write-Host "(Ensure your CA certificate is installed on client machines.)"

Remove-Item $TempDir -Recurse -Force -ErrorAction SilentlyContinue
