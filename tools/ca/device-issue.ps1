<#
.SYNOPSIS
  Issue a device certificate signed by the fleet CA.

.DESCRIPTION
  Generates an EC P-256 device key and a CA-signed certificate with the device's
  mDNS hostname as a Subject Alternative Name. The certificate is valid 825 days.

.EXAMPLE
  # Get the suffix from the device's boot log (last 2 MAC bytes as 4 hex chars):
  .\device-issue.ps1 -Suffix a1b2
#>
param(
  [Parameter(Mandatory=$true)]
  [string]$Suffix,            # 4 hex chars, e.g. "a1b2"

  [string]$CaKeyFile  = (Join-Path $PSScriptRoot "ca.key"),
  [string]$CaCertFile = (Join-Path $PSScriptRoot "ca.crt"),
  [string]$OutDir     = $PSScriptRoot,
  [int]   $Days       = 825
)

$ErrorActionPreference = "Stop"
$Suffix = $Suffix.ToLower()
$hostname = "weather-$Suffix"

$keyFile  = Join-Path $OutDir "device.key"
$csrFile  = Join-Path $OutDir "_device.csr"
$certFile = Join-Path $OutDir "device.crt"
$extFile  = Join-Path $OutDir "_device_ext.cnf"

if (!(Test-Path $CaKeyFile))  { throw "CA key not found: $CaKeyFile. Run ca-create.ps1 first." }
if (!(Test-Path $CaCertFile)) { throw "CA cert not found: $CaCertFile." }

@"
[req]
distinguished_name = dn
prompt             = no

[dn]
CN = $hostname
"@ | Out-File -FilePath (Join-Path $OutDir "_device_req.cnf") -Encoding ascii

@"
subjectAltName = DNS:${hostname}.local
extendedKeyUsage = serverAuth
"@ | Out-File -FilePath $extFile -Encoding ascii

try {
    & openssl ecparam -name prime256v1 -genkey -noout -out $keyFile
    if ($LASTEXITCODE -ne 0) { throw "ecparam failed" }

    & openssl req -new -key $keyFile -out $csrFile `
      -config (Join-Path $OutDir "_device_req.cnf")
    if ($LASTEXITCODE -ne 0) { throw "req -new failed" }

    & openssl x509 -req -in $csrFile -CA $CaCertFile -CAkey $CaKeyFile `
      -CAcreateserial -out $certFile -days $Days -sha256 -extfile $extFile
    if ($LASTEXITCODE -ne 0) { throw "x509 signing failed" }

    Write-Host ""
    Write-Host "Device certificate issued for: ${hostname}.local"
    Write-Host "  Private key  : $keyFile"
    Write-Host "  Certificate  : $certFile"
    Write-Host ""
    Write-Host "Next step: run device-provision.ps1 -Suffix $Suffix to flash these to the device."
} finally {
    Remove-Item $csrFile, $extFile, (Join-Path $OutDir "_device_req.cnf"), `
                (Join-Path $OutDir "ca.srl") -ErrorAction SilentlyContinue
}
