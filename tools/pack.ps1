# pack.ps1 — build the .wsplugin package.
#
# A .wsplugin is a zip whose root (or its single top folder) holds plugin.json
# beside the files the manifest names (WinShun/src/plugin/Install.cpp). This
# script copies exactly those files from the last build into dist/ and zips them
# with Windows' own Compress-Archive, so no extra tool is needed.
#
# Usage: pwsh -File tools\pack.ps1 [-Configuration Release] [-BuildDir build-qt]
# The Qt build is the one to ship: the window is the plugin's point.

param(
    [string]$BuildDir = "build-qt",
    [string]$OutDir = "dist"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

$exe = Join-Path $root "rename.exe"
if (-not (Test-Path $exe)) {
    Write-Error "rename.exe not found next to the sources; build first (cmake --build $BuildDir)"
}

$manifest = Get-Content (Join-Path $root "package\plugin.json") -Raw | ConvertFrom-Json
$id = $manifest.id
$version = $manifest.version

$stage = Join-Path $env:TEMP ("wsplugin-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $stage | Out-Null
try {
    Copy-Item $exe (Join-Path $stage "rename.exe")
    Copy-Item (Join-Path $root "package\plugin.json") (Join-Path $stage "plugin.json")

    # The exe must be the Qt build when the manifest declares runtime.qt: host;
    # a headless exe would start and then never show a window.
    $imports = & {
        $dumpbin = Get-ChildItem "C:\Program Files\Microsoft Visual Studio" -Recurse -Filter "dumpbin.exe" -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match 'Hostx64\\x64' } | Select-Object -First 1
        if ($dumpbin) { & $dumpbin.FullName /dependents $exe 2>&1 | Out-String }
    }
    if ($manifest.runtime.qt -eq "host" -and $imports -notmatch "Qt6Core\.dll") {
        Write-Error "plugin.json declares runtime.qt: host but rename.exe does not import Qt6Core.dll; build with -DRENAME_BUILD_QT=ON"
    }

    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null | Out-Null
    $zip = Join-Path $root "$OutDir\$id-$version.wsplugin"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip

    $sha = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
    Write-Output "package: $zip"
    Write-Output ("size:    {0:N0} bytes" -f (Get-Item $zip).Length)
    Write-Output "sha256:  $sha"
} finally {
    Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
}
