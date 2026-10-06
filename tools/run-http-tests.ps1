param([string]$Compiler='gcc')
$ErrorActionPreference='Stop'
$plexRoot=Split-Path $PSScriptRoot -Parent
$plexScratch=Join-Path $plexRoot ('build/http-tests-'+[guid]::NewGuid())
$plexMock=Join-Path $plexScratch 'include'
foreach($plexHeader in @('kernel/sysmem.h','io/fcntl.h','net/net.h','net/netctl.h','net/http.h','libssl.h','sysmodule.h','kernel/threadmgr.h','kernel/processmgr.h')) {
 $plexTarget=Join-Path $plexMock ('psp2/'+$plexHeader)
 New-Item -ItemType Directory -Path (Split-Path $plexTarget -Parent) -Force | Out-Null
 Set-Content -LiteralPath $plexTarget -Value '#include <psp2/http_mock.h>'
}
Push-Location $plexRoot
try {
 $plexExe=Join-Path $plexScratch 'http_tests.exe'
 & $Compiler -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -D__vita__ "-I$plexMock" -Itools/mock-vita -Isrc tools/http_lifecycle_test.c src/http.c -o $plexExe
 if($LASTEXITCODE){throw 'HTTP test compilation failed'}
 & $plexExe
 if($LASTEXITCODE){throw 'HTTP tests failed'}
} finally {Pop-Location}
