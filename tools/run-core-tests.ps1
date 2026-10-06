param([string]$Compiler='gcc')
$ErrorActionPreference='Stop'
$plexRoot=Split-Path $PSScriptRoot -Parent
$plexScratch=Join-Path $plexRoot ('build/core-tests-'+[guid]::NewGuid())
$plexInclude=Join-Path $plexScratch 'include/psp2/kernel'
New-Item -ItemType Directory -Path $plexInclude -Force | Out-Null
Set-Content -LiteralPath (Join-Path $plexInclude 'threadmgr.h') -Value '#include <psp2/mock.h>'
Set-Content -LiteralPath (Join-Path $plexScratch 'include/psp2/types.h') -Value '#include <psp2/mock.h>'
Set-Content -LiteralPath (Join-Path $plexScratch 'include/psp2/power.h') -Value '#include <psp2/mock.h>'
Push-Location $plexRoot
try {
 $plexExe=Join-Path $plexScratch 'core_tests.exe'
 & $Compiler -std=c11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -D__vita__ -pthread "-I$plexScratch/include" -Itools/mock-vita -Isrc tools/parallel_video_test.c src/video.c src/performance.c -o $plexExe
 if($LASTEXITCODE){throw 'Core test compilation failed'}
 & $plexExe
 if($LASTEXITCODE){throw 'Core tests failed'}
 & $Compiler -std=c11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -D__vita__ -DPLEX_TEST_JOURNAL -pthread "-I$plexScratch/include" -Itools/mock-vita -Isrc tools/progress_recovery_test.c src/progress.c src/video.c src/performance.c src/plex_auth.c -o "$plexScratch/progress_test.exe"
 if($LASTEXITCODE){throw 'Recovery test compilation failed'}
 Push-Location $plexScratch
 try {
  ./progress_test.exe
  if($LASTEXITCODE){throw 'Journal test failed'}
  ./progress_test.exe recover
  if($LASTEXITCODE){throw 'Recovery test failed'}
 } finally {Pop-Location}
} finally {Pop-Location}
