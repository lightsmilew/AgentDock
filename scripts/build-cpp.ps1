param([switch]$Test, [string]$Configuration = 'Release', [string]$BuildDirectory = 'build\cpp', [string]$Target = 'all')
$ErrorActionPreference = 'Stop'
$env:VSLANG = '1033'
$workspace = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'Visual Studio C++ Build Tools not found' }
$devcmd = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
# Import the environment from the installed VS toolchain, no persistent environment changes.
$lines = & $env:ComSpec /d /c "`"$devcmd`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
foreach ($line in $lines) { if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') } }
$env:VSLANG = '1033'
& cmake -S (Join-Path $workspace 'cpp') -B (Join-Path $workspace $BuildDirectory) -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration"
if ($LASTEXITCODE) { throw 'CMake configure failed' }
& cmake --build (Join-Path $workspace $BuildDirectory) --target $Target --parallel 4
if ($LASTEXITCODE) { throw 'C++ build failed' }
if ($Test) {
  & ctest --test-dir (Join-Path $workspace $BuildDirectory) --output-on-failure
  if ($LASTEXITCODE) { throw 'Native tests failed' }
}
