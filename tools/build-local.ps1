<#
.SYNOPSIS
  Build Marlin locally from an AnglerOS-generated config.ini for the SKR Pico.

.DESCRIPTION
  Clones Marlin at the release the config.ini targets, drops the config.ini in
  place (Marlin auto-applies it via its pre-build configuration.py script), and
  runs a PlatformIO build for the SKR_Pico env. Use this to verify that the web
  interface generates a real, buildable Marlin configuration.

.EXAMPLE
  ./tools/build-local.ps1 -Config "$HOME\Downloads\config.ini"

.EXAMPLE
  ./tools/build-local.ps1 -Config .\config.ini -Tag bugfix-2.1.x
#>
param(
  [Parameter(Mandatory = $true)] [string]$Config,
  [string]$WorkDir = ".\marlin-build",
  [string]$Tag,
  [string]$Env = "SKR_Pico"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $Config)) { throw "Config not found: $Config" }

# Derive the Marlin ref from the config.ini header if not given.
if (-not $Tag) {
  $m = Select-String -Path $Config -Pattern 'Marlin source tag:\s*(\S+)' | Select-Object -First 1
  if ($m) { $Tag = $m.Matches[0].Groups[1].Value }
  if (-not $Tag -or $Tag -eq 'bugfix-2.1.x') { $Tag = 'bugfix-2.1.x' }
}
Write-Host "Marlin ref: $Tag" -ForegroundColor Cyan

# Clone Marlin (shallow) at that ref if we don't already have it.
if (-not (Test-Path $WorkDir)) {
  Write-Host "Cloning MarlinFirmware/Marlin @ $Tag ..." -ForegroundColor Cyan
  git clone --depth 1 --branch $Tag https://github.com/MarlinFirmware/Marlin.git $WorkDir
  if ($LASTEXITCODE -ne 0) { throw "git clone failed (is '$Tag' a valid Marlin tag/branch?)" }
} else {
  Write-Host "Reusing existing $WorkDir (delete it to re-clone a different ref)." -ForegroundColor Yellow
}

# Drop our config.ini where Marlin's pre-build script picks it up.
Copy-Item $Config (Join-Path $WorkDir "Marlin\config.ini") -Force
Write-Host "Copied config.ini -> $WorkDir\Marlin\config.ini" -ForegroundColor Green

# Locate the PlatformIO CLI (PATH, then the VS Code extension's bundled penv).
$pio = (Get-Command pio -ErrorAction SilentlyContinue).Source
if (-not $pio) { $pio = (Get-Command platformio -ErrorAction SilentlyContinue).Source }
if (-not $pio) {
  $penv = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\platformio.exe"
  if (Test-Path $penv) { $pio = $penv }
}

if (-not $pio) {
  Write-Host ""
  Write-Host "PlatformIO CLI not found. Open '$WorkDir' in VS Code, then in the" -ForegroundColor Yellow
  Write-Host "PlatformIO sidebar build the '$Env' environment. The config.ini is" -ForegroundColor Yellow
  Write-Host "already in place." -ForegroundColor Yellow
  return
}

Write-Host "Building env:$Env with $pio ..." -ForegroundColor Cyan
Push-Location $WorkDir
try { & $pio run -e $Env } finally { Pop-Location }

$uf2 = Join-Path $WorkDir ".pio\build\$Env\firmware.uf2"
if (Test-Path $uf2) {
  Write-Host ""
  Write-Host "BUILD OK -> $uf2" -ForegroundColor Green
  Write-Host "Flash: hold BOOT on the SKR Pico, plug in USB (mounts as RPI-RP2)," -ForegroundColor Green
  Write-Host "then copy that firmware.uf2 onto the drive." -ForegroundColor Green
} else {
  Write-Host "Build finished but no firmware.uf2 found - check the log above." -ForegroundColor Red
}
