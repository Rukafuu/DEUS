[CmdletBinding()]
param(
    [string]$BuildDirectory = "build",
    [string]$Configuration = "Release",
    [string]$Architecture = "x64",
    [string]$OutputDirectory = "dist",
    [string]$CMakePath,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$buildPath = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $BuildDirectory))
$outputPath = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $OutputDirectory))

if (-not (Test-Path -LiteralPath $buildPath -PathType Container)) {
    throw "Build directory not found: $buildPath. Configure it with CMake first."
}

$cmakeCache = Join-Path $buildPath "CMakeCache.txt"
if (-not $CMakePath) {
    $cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmakeCommand) {
        $CMakePath = $cmakeCommand.Source
    }
    elseif (Test-Path -LiteralPath $cmakeCache) {
        $cachedCommand = Select-String -LiteralPath $cmakeCache -Pattern '^CMAKE_COMMAND:INTERNAL=(.+)$' |
            Select-Object -First 1
        if ($cachedCommand) {
            $CMakePath = $cachedCommand.Matches[0].Groups[1].Value
        }
    }
}

if (-not $CMakePath -or -not (Test-Path -LiteralPath $CMakePath -PathType Leaf)) {
    throw "CMake was not found. Add it to PATH or pass -CMakePath."
}

$cmakeLists = Get-Content -Raw -LiteralPath (Join-Path $repositoryRoot "CMakeLists.txt")
$versionMatch = [regex]::Match($cmakeLists, 'project\s*\(\s*DEUS\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)')
if (-not $versionMatch.Success) {
    throw "Could not read the DEUS version from CMakeLists.txt."
}
$version = $versionMatch.Groups[1].Value
$packageName = "deus-$version-windows-$Architecture"

New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("deus-package-" + [guid]::NewGuid().ToString("N"))
$stageRoot = Join-Path $temporaryRoot $packageName
$zipPath = Join-Path $outputPath "$packageName.zip"
$checksumPath = "$zipPath.sha256"

try {
    New-Item -ItemType Directory -Force -Path $stageRoot | Out-Null

    if (-not $SkipBuild) {
        & $CMakePath --build $buildPath --config $Configuration
        if ($LASTEXITCODE -ne 0) { throw "CMake build failed with exit code $LASTEXITCODE." }
    }

    & $CMakePath --install $buildPath --config $Configuration --prefix $stageRoot
    if ($LASTEXITCODE -ne 0) { throw "CMake install failed with exit code $LASTEXITCODE." }

    foreach ($directory in @("docs", "examples")) {
        Copy-Item -LiteralPath (Join-Path $repositoryRoot $directory) -Destination $stageRoot -Recurse
    }
    foreach ($file in @("README.md", "DESIGN.md")) {
        $source = Join-Path $repositoryRoot $file
        if (Test-Path -LiteralPath $source -PathType Leaf) {
            Copy-Item -LiteralPath $source -Destination $stageRoot
        }
    }

    $extensionManifestPath = Join-Path $repositoryRoot "editors\vscode\package.json"
    $extensionManifest = Get-Content -Raw -LiteralPath $extensionManifestPath | ConvertFrom-Json
    $vsixName = "$($extensionManifest.name)-$($extensionManifest.version).vsix"
    $vsixPath = Join-Path $repositoryRoot "editors\vscode\$vsixName"
    if (-not (Test-Path -LiteralPath $vsixPath -PathType Leaf)) {
        throw "Expected VS Code package not found: $vsixPath"
    }
    $editorPath = Join-Path $stageRoot "editors\vscode"
    New-Item -ItemType Directory -Force -Path $editorPath | Out-Null
    Copy-Item -LiteralPath $vsixPath -Destination $editorPath

    foreach ($executable in @("deus.exe", "deusc.exe", "deusvm.exe", "deus-language-server.exe")) {
        if (-not (Test-Path -LiteralPath (Join-Path $stageRoot "bin\$executable") -PathType Leaf)) {
            throw "Installed package is missing bin\$executable."
        }
    }

    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
    $archive = [System.IO.Compression.ZipFile]::Open($zipPath, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        $files = Get-ChildItem -LiteralPath $stageRoot -File -Recurse |
            Sort-Object { $_.FullName.Substring($temporaryRoot.Length) }
        foreach ($file in $files) {
            $entryName = $file.FullName.Substring($temporaryRoot.Length + 1).Replace('\', '/')
            $entry = $archive.CreateEntry($entryName, [System.IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime = [datetimeoffset]::new(1980, 1, 1, 0, 0, 0, [timespan]::Zero)
            $inputStream = $file.OpenRead()
            $outputStream = $entry.Open()
            try { $inputStream.CopyTo($outputStream) }
            finally {
                $outputStream.Dispose()
                $inputStream.Dispose()
            }
        }
    }
    finally { $archive.Dispose() }

    $hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    [System.IO.File]::WriteAllText($checksumPath, "$hash  $([System.IO.Path]::GetFileName($zipPath))`n", [System.Text.UTF8Encoding]::new($false))

    Write-Host "Created $zipPath"
    Write-Host "Created $checksumPath"
}
finally {
    if (Test-Path -LiteralPath $temporaryRoot) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
    }
}
