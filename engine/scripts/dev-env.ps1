# Loads the MSVC x64 environment and the vcpkg that ships with Visual Studio.
# Usage, from any PowerShell:  . engine\scripts\dev-env.ps1
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = if (Test-Path $vswhere) {
    & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $vs) { $vs = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools" }

Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null
$env:VCPKG_ROOT = "$vs\VC\vcpkg"
Write-Host "MSVC: $((Get-Command cl).Source)"
