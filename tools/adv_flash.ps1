# Put the built firmware on the Cardputer ADV over USB.
# Usage:  .\tools\adv_flash.ps1 [-Port COM8] [-Factory]
#   default  = update (keeps settings, nodes, messages): writes the app at 0x10000
#   -Factory = first install / full wipe: writes the factory .bin at 0x0
# Needs esptool:  python -m pip install esptool   (set $env:ESPTOOL_PY to a python.exe that has it)
param([string]$Port = "COM8", [switch]$Factory)
$py = if ($env:ESPTOOL_PY) { $env:ESPTOOL_PY } else { "python" }
$out = Join-Path $PSScriptRoot "..\out\adv"
if ($Factory) {
    $bin = Get-ChildItem $out -Filter "firmware-*.factory.bin" | Select-Object -First 1; $addr = "0x0"
} else {
    $bin = Get-ChildItem $out -Filter "firmware-*.bin" | Where-Object { $_.Name -notmatch 'factory' } | Select-Object -First 1; $addr = "0x10000"
}
if (-not $bin) { Write-Error "No firmware in $out - run tools/adv_build.sh first"; exit 1 }
& $py -m esptool --chip esp32s3 --port $Port --baud 921600 write_flash $addr $bin.FullName
