param([string]$Compiler = 'gcc')
$ErrorActionPreference = 'Stop'
$plexRoot = Split-Path $PSScriptRoot -Parent
$plexScratch = Join-Path $plexRoot ('build/host-tests-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $plexScratch | Out-Null
Push-Location $plexRoot
try {
  $plexExe = Join-Path $plexScratch 'regression_test.exe'
  & $Compiler -std=c11 -Wall -Wextra -Werror -Isrc tools/regression_test.c src/plex_auth.c src/plex.c src/browse.c src/settings.c src/update.c -o $plexExe
  if ($LASTEXITCODE) { throw 'Regression test compilation failed' }
  Push-Location $plexScratch
  try {
    & $plexExe
    if ($LASTEXITCODE) { throw 'Regression tests failed' }
  } finally { Pop-Location }
} finally { Pop-Location }
