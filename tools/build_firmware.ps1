param(
    [string]$Fqbn = 'esp32:esp32:esp32:PartitionScheme=min_spiffs',
    [string]$ArduinoData = (Join-Path $env:USERPROFILE 'AppData\Local\Arduino15'),
    [string]$ArduinoCli = (Join-Path $env:USERPROFILE 'AppData\Local\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe')
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $projectRoot '.build'
$cliConfig = Join-Path $buildRoot 'arduino-cli.yaml'
New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
& python (Join-Path $PSScriptRoot 'embed_dashboard.py')
if ($LASTEXITCODE -ne 0) { throw 'Dashboard embedding failed' }
@{
    directories = @{
        data = $ArduinoData
        downloads = (Join-Path $buildRoot 'downloads')
        user = (Join-Path $buildRoot 'arduino-user')
    }
} | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $cliConfig -Encoding utf8
# JSON is valid YAML; keeping state under .build avoids changing IDE settings.
& $ArduinoCli --config-file $cliConfig compile --fqbn $Fqbn `
    --build-path (Join-Path $buildRoot 'esp32') `
    --output-dir (Join-Path $projectRoot 'dist') `
    (Join-Path $projectRoot 'HyundaiAudisonBridge')
if ($LASTEXITCODE -ne 0) { throw 'ESP32 compilation failed' }
Write-Output 'Application OTA binary: dist\HyundaiAudisonBridge.ino.bin'
