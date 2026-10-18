$ErrorActionPreference = "Stop"

$msys2_root = if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } else { "C:\msys64" }

$PACKAGES = @(
    "mingw-w64-ucrt-x86_64-gcc"
    "mingw-w64-ucrt-x86_64-cmake"
    "mingw-w64-ucrt-x86_64-ninja"
    "mingw-w64-ucrt-x86_64-pkgconf"
    "mingw-w64-ucrt-x86_64-python"

    "mingw-w64-ucrt-x86_64-vulkan-headers"
    "mingw-w64-ucrt-x86_64-vulkan-loader"

    "mingw-w64-ucrt-x86_64-glslang"
    "mingw-w64-ucrt-x86_64-spirv-tools"
    "mingw-w64-ucrt-x86_64-spirv-headers"
)

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    winget install --id Git.Git -e --accept-package-agreements --accept-source-agreements
}

if (-not (Test-Path "$msys2_root\usr\bin\bash.exe")) {
    winget install --id MSYS2.MSYS2 -e --accept-package-agreements --accept-source-agreements --location $msys2_root
}

$bash = "$msys2_root\usr\bin\bash.exe"

& $bash -lc "pacman -Syu --noconfirm"
& $bash -lc "pacman -Syu --noconfirm"
& $bash -lc "pacman -S --needed --noconfirm $($PACKAGES -join ' ')"

if ($LASTEXITCODE -ne 0) {
    throw "pacman failed"
}

Write-Host "toolchain: $msys2_root\ucrt64\bin"
