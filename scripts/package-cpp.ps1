param([string]$BuildDirectory = 'build\cpp', [string]$DestinationDirectory = 'dist\AgentDock-native')
$ErrorActionPreference = 'Stop'
$workspace = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $workspace "$BuildDirectory\AgentDock.exe"
if (-not (Test-Path -LiteralPath $executable)) { throw 'Build AgentDock first with scripts/build-cpp.ps1 -Test' }
$destination = Join-Path $workspace $DestinationDirectory
New-Item -ItemType Directory -Path $destination -Force | Out-Null
Copy-Item -LiteralPath $executable -Destination (Join-Path $destination 'AgentDock.exe') -Force
$portableReadme = Get-Content -LiteralPath (Join-Path $workspace 'cpp\PORTABLE.md') -Raw -Encoding utf8
$portableReadme.Replace('(../docs/', '(docs/') | Set-Content -LiteralPath (Join-Path $destination 'README.md') -Encoding utf8
New-Item -ItemType Directory -Path (Join-Path $destination 'docs') -Force | Out-Null
$documentation = @(Get-ChildItem -LiteralPath (Join-Path $workspace 'docs') -Filter '*.md' -File | Sort-Object Name)
foreach ($document in $documentation) {
  Copy-Item -LiteralPath $document.FullName -Destination (Join-Path $destination "docs\$($document.Name)") -Force
}
$notices = @(
  '# Third-party notices',
  '',
  'SQLite 3.54.1 is public domain: https://www.sqlite.org/copyright.html',
  'Brand names and marks belong to their owners; no affiliation is implied.',
  'Brand asset provenance is recorded in brand-sources.json.',
  ''
)
foreach ($name in 'json','toml','icons') {
  $notices += "## $name"
  $notices += Get-Content -LiteralPath (Join-Path $workspace "cpp\vendor\$name.LICENSE") -Raw
}
$notices += '## libyaml 0.2.5'
$notices += Get-Content -LiteralPath (Join-Path $workspace 'cpp\vendor\yaml\License') -Raw
$notices -join "`r`n" | Set-Content -LiteralPath (Join-Path $destination 'THIRD-PARTY-NOTICES.txt') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $workspace 'assets\brands\sources.json') -Destination (Join-Path $destination 'brand-sources.json') -Force
$archive = Join-Path $workspace 'dist\AgentDock-Native-Windows-x64.zip'
# Include only the declared release files, even if the destination contains old
# logs, screenshots or documentation from an earlier packaging run.
$releaseFiles = @('AgentDock.exe','README.md','THIRD-PARTY-NOTICES.txt','brand-sources.json')
$releaseFiles += $documentation | ForEach-Object { 'docs/' + $_.Name }
New-Item -ItemType Directory -Path (Split-Path -Parent $archive) -Force | Out-Null
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$temporaryArchive = $archive + '.' + [Guid]::NewGuid().ToString('N') + '.tmp'
$zip = [IO.Compression.ZipFile]::Open($temporaryArchive, [IO.Compression.ZipArchiveMode]::Create)
try {
  foreach ($relative in $releaseFiles) {
    [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, (Join-Path $destination $relative), $relative, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
  }
} finally { $zip.Dispose() }
Move-Item -LiteralPath $temporaryArchive -Destination $archive -Force
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  AgentDock-Native-Windows-x64.zip" | Set-Content -LiteralPath ($archive + '.sha256') -Encoding ascii
Get-Item -LiteralPath $executable,$archive | Select-Object Name,Length
