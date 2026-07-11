<#
.SYNOPSIS
  Build a LittleFS image with the device cert+key and flash it to the storage partition.

.EXAMPLE
  .\device-provision.ps1 -Suffix 1ea4 -Port COM3
#>
param(
  [Parameter(Mandatory=$true)] [string]$Suffix,
  [Parameter(Mandatory=$true)] [string]$Port,
  [string]$KeyFile,
  [string]$CertFile,
  [string]$TempDir,
  [string]$ImageFile
)

$ErrorActionPreference = "Stop"

# Defaults resolved here so $PSScriptRoot is guaranteed set
if (!$KeyFile)   { $KeyFile   = Join-Path $PSScriptRoot "device.key" }
if (!$CertFile)  { $CertFile  = Join-Path $PSScriptRoot "device.crt" }
if (!$TempDir)   { $TempDir   = Join-Path $PSScriptRoot "_provision_tmp" }
if (!$ImageFile) { $ImageFile = Join-Path $PSScriptRoot "storage.bin" }

if (!(Test-Path $KeyFile))  { throw "device.key not found at $KeyFile. Run device-issue.ps1 first." }
if (!(Test-Path $CertFile)) { throw "device.crt not found at $CertFile. Run device-issue.ps1 first." }

# Build staging directory
if (Test-Path $TempDir) { Remove-Item $TempDir -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $TempDir "certs") | Out-Null
Copy-Item $CertFile (Join-Path $TempDir "certs\device.crt")
Copy-Item $KeyFile  (Join-Path $TempDir "certs\device.key")

# Create LittleFS image using littlefs-python
# Storage partition: size 0x9E0000 = 10354688 bytes, block_size 4096
Write-Host "Building LittleFS image..."
$py = @"
import littlefs, os, sys

src   = sys.argv[1]
out   = sys.argv[2]
size  = 0x9E0000        # storage partition size from partitions.csv
bsize = 4096            # SPI flash erase block

fs = littlefs.LittleFS(block_size=bsize, block_count=size // bsize)
fs.mkdir('/certs')
for fname in os.listdir(os.path.join(src, 'certs')):
    fpath = os.path.join(src, 'certs', fname)
    with open(fpath, 'rb') as f:
        data = f.read()
    with fs.open('/certs/' + fname, 'wb') as f:
        f.write(data)
with open(out, 'wb') as f:
    f.write(bytes(fs.context.buffer))
print(f'Image written: {out}  ({len(fs.context.buffer)} bytes)')
"@

python -c $py $TempDir $ImageFile
if ($LASTEXITCODE -ne 0) { throw "LittleFS image creation failed" }

# Flash using esptool
Write-Host "Flashing storage partition at offset 0x620000 on $Port ..."
python -m esptool --chip esp32 --port $Port --baud 921600 `
    write-flash 0x620000 $ImageFile
if ($LASTEXITCODE -ne 0) { throw "esptool flash failed" }

Remove-Item $TempDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "Provisioned weather-$Suffix - HTTPS available after reboot."
Write-Host "Ensure ca.crt is installed as a trusted root on client machines."
