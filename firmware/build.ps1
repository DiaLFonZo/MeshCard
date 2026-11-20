# Build the U115 bridge firmware and copy the outputs to C:\Users\enant\tools\u115-fw
$ErrorActionPreference = 'Stop'
$cli  = "C:\Users\enant\tools\arduino-cli\arduino-cli.exe"
$proj = Join-Path $PSScriptRoot 'u115_bridge'
$out  = "C:\Users\enant\tools\u115-fw"
$fqbn = 'CubeCell:CubeCell:CubeCell-Module:LORAWAN_REGION=9,LORAWAN_AT_SUPPORT=1'

& $cli compile -b $fqbn --output-dir "$proj\build" `
    --build-property "compiler.c.elf.extra_flags=-Wl,--wrap=boardInitMcu,--wrap=cyfitter_cfg" $proj
if ($LASTEXITCODE -ne 0) { throw "build failed" }
New-Item -ItemType Directory -Force $out | Out-Null
Copy-Item "$proj\build\u115_bridge.ino.hex" "$out\bridge.hex" -Force
Copy-Item "$proj\build\u115_bridge.ino.elf" "$out\bridge.elf" -Force
"built -> $out\bridge.hex"
