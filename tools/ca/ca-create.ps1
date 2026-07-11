<#
.SYNOPSIS
  Create a private Certificate Authority for the weather station fleet.

.DESCRIPTION
  Generates an EC P-256 CA key (AES-256 encrypted) and a self-signed CA
  certificate valid for 20 years. You will be prompted for a passphrase.
  Keep ca.key private - never copy it to devices.
  Distribute ca.crt to client machines/browsers that need to trust device HTTPS.

.EXAMPLE
  .\ca-create.ps1 -Org "My Home"
#>
param(
  [string]$Org    = "WeatherStation CA",
  [string]$OutDir = $PSScriptRoot
)

$ErrorActionPreference = "Stop"

$keyFile  = Join-Path $OutDir "ca.key"
$certFile = Join-Path $OutDir "ca.crt"
$cnfFile  = Join-Path $OutDir "_ca.cnf"

if (Test-Path $keyFile) {
    Write-Warning "ca.key already exists at $keyFile - skipping to avoid overwrite."
    Write-Warning "Delete it manually if you want a new CA."
    exit 0
}

@"
[req]
distinguished_name = dn
x509_extensions    = v3_ca
prompt             = no

[dn]
CN = $Org
O  = $Org

[v3_ca]
subjectKeyIdentifier   = hash
authorityKeyIdentifier = keyid:always,issuer
basicConstraints       = critical, CA:TRUE
keyUsage               = critical, keyCertSign, cRLSign
"@ | Out-File -FilePath $cnfFile -Encoding ascii

try {
    # Generate EC P-256 key, pipe directly into AES-256 encryption.
    # openssl will prompt for a passphrase twice.
    & openssl ecparam -name prime256v1 -genkey -noout |
        & openssl ec -aes256 -out $keyFile
    if ($LASTEXITCODE -ne 0) { throw "openssl key generation failed" }

    # Self-signed CA certificate (7300 days = ~20 years)
    & openssl req -new -x509 -key $keyFile -out $certFile `
      -days 7300 -sha256 -config $cnfFile
    if ($LASTEXITCODE -ne 0) { throw "openssl req failed" }

    Write-Host ""
    Write-Host "CA created successfully:"
    Write-Host "  Private key : $keyFile   (KEEP PRIVATE - NEVER COPY TO DEVICE)"
    Write-Host "  Certificate : $certFile  (install on clients)"
    Write-Host ""
    Write-Host "To install the CA certificate:"
    Write-Host "  Windows : certlm.msc -> Trusted Root CAs -> Import $certFile"
    Write-Host "  macOS   : sudo security add-trusted-cert -d -r trustRoot -k /Library/Keychains/System.keychain $certFile"
    Write-Host "  Linux   : sudo cp $certFile /usr/local/share/ca-certificates/ && sudo update-ca-certificates"
} finally {
    Remove-Item $cnfFile -ErrorAction SilentlyContinue
}
