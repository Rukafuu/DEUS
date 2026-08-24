# Local Windows release

This procedure creates a local, unsigned DEUS distribution. It does not upload,
publish, sign, or download anything.

## Prerequisites

- a configured CMake build directory;
- Visual Studio Build Tools and CMake as described in the main README;
- a prebuilt VS Code package matching the version in
  `editors/vscode/package.json`.

The packaging script deliberately does not run `npm`, `npx`, `vsce`, or any
network command. Build the VSIX separately before a release when its version or
contents change.

## Create the package

From the repository root:

```powershell
.\scripts\package-windows.ps1 -BuildDirectory build -Configuration Release
```

The script builds the configured CMake tree, installs its runtime targets and
assets into an isolated staging directory, then adds documentation, examples,
and the version-matched VSIX. Output is written to `dist/` by default:

```text
deus-<version>-windows-x64.zip
deus-<version>-windows-x64.zip.sha256
```

Pass `-SkipBuild` to package existing binaries, `-OutputDirectory` to select a
different repository-relative destination, or `-CMakePath` when CMake is not on
`PATH` and cannot be recovered from the selected build cache.

The ZIP uses sorted paths and a fixed entry timestamp. Given identical installed
binaries and source artifacts, repeated packaging produces the same SHA-256.

## Inspect and verify

List the archive without extracting it:

```powershell
tar -tf .\dist\deus-0.1.0-windows-x64.zip
```

Verify its checksum:

```powershell
$zip = '.\dist\deus-0.1.0-windows-x64.zip'
$expected = (Get-Content "$zip.sha256").Split(' ')[0]
$actual = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actual -ne $expected) { throw 'SHA-256 mismatch' }
```

Before any public release, also run the full test suite, inspect the archive
contents, test the executables on a clean supported Windows environment, and
decide on signing and publication as separate, explicitly authorized steps.
