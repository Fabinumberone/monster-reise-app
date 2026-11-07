# Reisen zwischen den Spielen (Azahar), bis die Reise-App fertig ist.
#   powershell -ExecutionPolicy Bypass -File tools\reise.ps1 -Von UM -Nach OR
# Spiele: OR, AS, X, Y, Sonne, Mond, US, UM
param(
    [Parameter(Mandatory = $true)][ValidateSet('OR', 'AS', 'X', 'Y', 'Sonne', 'Mond', 'US', 'UM')][string]$Von,
    [Parameter(Mandatory = $true)][ValidateSet('OR', 'AS', 'X', 'Y', 'Sonne', 'Mond', 'US', 'UM')][string]$Nach
)
$ErrorActionPreference = 'Stop'

$titel = @{ OR = '0011c400'; AS = '0011c500'; X = '00055d00'; Y = '00055e00';
            Sonne = '00164800'; Mond = '00175e00'; US = '001b5000'; UM = '001b5100' }
$sd = Join-Path $env:APPDATA 'Azahar\sdmc'
$basis = Join-Path $sd 'Nintendo 3DS\00000000000000000000000000000000\00000000000000000000000000000000\title\00040000'
$speicher = Join-Path $sd 'pokemon-world-3d\speicher.bin'
$sicherungen = Join-Path $sd 'pokemon-world-3d\sicherungen'
$exe = Join-Path $PSScriptRoot '..\build\pw3d-reise.exe'

function Spielstand($name) { Join-Path $basis "$($titel[$name])\data\00000001\main" }
$vonDatei = Spielstand $Von
$nachDatei = Spielstand $Nach
foreach ($d in @($vonDatei, $nachDatei, $speicher, $exe)) {
    if (-not (Test-Path $d)) { Write-Host "Nicht gefunden: $d" -ForegroundColor Red; exit 1 }
}

# Läuft gerade ein Spiel? Dann steht im Fenstertitel „Azahar … | Spielname“.
$az = Get-Process azahar -ErrorAction SilentlyContinue
if ($az -and ($az.MainWindowTitle -match '\|')) {
    Write-Host "In Azahar läuft gerade ein Spiel ($($az.MainWindowTitle)). Bitte erst speichern und das Spiel beenden." -ForegroundColor Red
    exit 1
}

# Sicherungen: beide Spielstände und der Speicher.
$zeit = Get-Date -Format 'yyyyMMdd-HHmmss'
New-Item -ItemType Directory -Force $sicherungen | Out-Null
Copy-Item $vonDatei (Join-Path $sicherungen "$zeit-$Von.main")
if ($Nach -ne $Von) { Copy-Item $nachDatei (Join-Path $sicherungen "$zeit-$Nach.main") }
Copy-Item $speicher (Join-Path $sicherungen "$zeit-speicher.bin")

& $exe reise $speicher $vonDatei $nachDatei
if ($LASTEXITCODE -ne 0) { Write-Host "Reise fehlgeschlagen, nichts verändert." -ForegroundColor Red; exit 1 }
# Für die Reise-App: in welchem Spiel man sich jetzt befindet.
[IO.File]::WriteAllText((Join-Path $sd 'pokemon-world-3d\zuletzt.txt'), "$($titel[$Nach])`n")
Write-Host "Gute Reise! Sicherungen liegen in $sicherungen" -ForegroundColor Green
