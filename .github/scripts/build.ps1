param(
    [switch]$Deploy
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

. "$PSScriptRoot\buildutils.ps1"

#region Initialize and prepare build/deploy env
. Initialize-BuildEnvironment
if ($IsWindows) {
    Initialize-VSDevShell -DevCmdArguments "-arch=x64 -host_arch=x64"
}
if ($IsMacOS) {
    Patch-KF6ToolsRpath
}
#endregion

#region Deploy
if ($Deploy) {
    if ($IsWindows) {
        $binaryPath = Join-Path $PWD.Path "install-msvc\bin\pineapple-chapter-marker.exe"
        $buildDir = Join-Path $PWD.Path "build-msvc"
        $kf6InstallDir = $env:KF6REDIST_DIR

        Deploy-WindowsApp -BinaryPath $binaryPath `
            -Kf6InstallDir $kf6InstallDir -BuildDir $buildDir `
            -QtDir $env:QT_ROOT_DIR `
            -Locales @("zh_CN") `
            -CopyVCRuntime
    } else {
        Write-Error "Deploy is not yet supported on this platform"
        exit 1
    }
    exit 0
}
#endregion

# Offline builds need no special flags anymore: CMake (cmake/PnDependencies.cmake)
# automatically consumes the archives under offline/ when that directory exists.

if ($IsWindows) {
    # Forward the FFmpeg/Qt locations to CMake: Qt's FindFFmpeg.cmake reads the
    # FFMPEG_DIR cache variable (it does not read environment variables).
    $cmakeArgs = @()
    Build-CMakeProject -SourcePath "." -RepoName "pineapple-chapter-marker" -ForceRebuild `
        -BuildBaseDir "build-msvc" -InstallPrefix "install-msvc" -NoSourceIdentifierFolder -AvoidReconfig `
        -CMakeArgs $cmakeArgs
} else {
    Write-Error "Build is not yet supported on this platform"
}
