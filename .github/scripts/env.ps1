if ($IsMacOS) {
    Write-Host "Not supported yet..."
} elseif ($IsLinux) {
    Write-Host "Not supported yet..."
} elseif ($IsWindows) {
    Write-Host "env.ps1 for Windows"
    $env:CMAKE_PREFIX_PATH = "$env:KF6REDIST_DIR"
}
