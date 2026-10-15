# Builds the release and writes build\DashMaid-<version>-win-x64.zip.
# The zip has one folder, DashMaid, with DashMaid.exe and the Settings folder.
# Files that the app does not use at run time are not in the zip.
# Run from any folder: pwsh -NoProfile -File tools\Package.ps1
$ErrorActionPreference = 'Stop'

$root = Split-Path $PSScriptRoot
$build = Join-Path $root 'build'
$cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'

function Check([string]$step) {
    if ($LASTEXITCODE -ne 0) { throw "$step failed (exit code $LASTEXITCODE)." }
}

$version = [regex]::Match((Get-Content (Join-Path $root 'CMakeLists.txt') -Raw), 'project\(DashMaid VERSION ([0-9.]+)').Groups[1].Value
if (-not $version) { throw 'Cannot find the version in CMakeLists.txt.' }

# The toolbar.
if (-not (Test-Path (Join-Path $build 'CMakeCache.txt'))) {
    & $cmake -S $root -B $build -G 'Visual Studio 18 2026' -A x64
    Check 'CMake configuration'
}
& $cmake --build $build --config Release --target DashMaid
Check 'The toolbar build'

$package = Join-Path $build 'package'
$stage = Join-Path $package 'DashMaid'
if (Test-Path $package) { Remove-Item -Recurse -Force $package }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item (Join-Path $build 'src\Toolbar\Release\DashMaid.exe') $stage

# The settings window. The C++ build environment can set LIB, and the C#
# compiler warns about its folders.
Remove-Item Env:LIB -ErrorAction SilentlyContinue
# Native AOT finds the linker with vswhere.exe, which is not on PATH.
$env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;$env:PATH"
dotnet publish (Join-Path $root 'src\Settings\DashMaid.Settings.csproj') -c Release --nologo -v:q -o (Join-Path $stage 'Settings')
Check 'The settings window publish'

# Remove the files that the app does not use at run time: debug symbols,
# metadata for compilers, the crash dump tool of .NET, the restart helper of
# the Windows App SDK, and the WinUI text in languages other than Chinese and
# English. The UI text of DashMaid is Chinese.
Get-ChildItem $stage -Recurse -File -Include *.pdb, *.winmd, createdump.exe, RestartAgent.exe | Remove-Item
$keep = 'zh-CN', 'zh-TW', 'en-us'
Get-ChildItem (Join-Path $stage 'Settings') -Directory |
    Where-Object { $_.Name -match '^[a-z]{2,3}(-[A-Za-z]+)+$' -and $keep -notcontains $_.Name } |
    Remove-Item -Recurse

$zip = Join-Path $build "DashMaid-$version-win-x64.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal
$size = (Get-Item $zip).Length / 1MB
"Wrote $zip ({0:N1} MB)" -f $size
