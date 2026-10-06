param([string]$Compiler='gcc')
$ErrorActionPreference='Stop'
$plexRoot=Split-Path $PSScriptRoot -Parent
$plexScratch=Join-Path $plexRoot ('build/player-tests-'+[guid]::NewGuid())
New-Item -ItemType Directory -Path $plexScratch | Out-Null
$plexMock=Join-Path $plexScratch 'include'
foreach($plexHeader in @('audioout.h','avplayer.h','ctrl.h','display.h','kernel/sysmem.h','io/fcntl.h','kernel/threadmgr.h','sysmodule.h','types.h')) {
  $plexTarget=Join-Path $plexMock ('psp2/'+$plexHeader)
  New-Item -ItemType Directory -Path (Split-Path $plexTarget -Parent) -Force | Out-Null
  Set-Content -LiteralPath $plexTarget -Value '#include <psp2/mock.h>'
}
Push-Location $plexRoot
try {
  $plexExe=Join-Path $plexScratch 'player_test.exe'
  & $Compiler -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -D__vita__ -Dmemalign=mock_memalign "-I$plexMock" -Itools/mock-vita -Isrc tools/player_lifecycle_test.c src/player.c -o $plexExe
  if($LASTEXITCODE){throw 'Player test compilation failed'}
  & $plexExe
  if($LASTEXITCODE){throw 'Player lifecycle tests failed'}
} finally {Pop-Location}
