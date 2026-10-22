$ErrorActionPreference = "Stop"

$ROOT_DIR = Split-Path -Parent $PSScriptRoot
Set-Location $ROOT_DIR

$build_type = "release"
$run_after  = $false

function usage {
    Write-Host "Usage: $PSCommandPath [options]"
    Write-Host
    Write-Host "Builds the project for Windows with MinGW (release by default)."
    Write-Host
    Write-Host "Options:"
    Write-Host "  -d, --debug  build in debug mode instead of release"
    Write-Host "  -r, --run    run the executable after a successful build"
    Write-Host "  -h, --help   show this help and exit"
    Write-Host
    Write-Host "Short options can be combined, e.g. -dr"
}

function bad_arg($arg) {
    Write-Error "Unknown argument: $arg`nRun '$PSCommandPath --help' for usage." -ErrorAction Continue
    exit 1
}

foreach ($arg in $args) {
    switch -CaseSensitive -Regex ($arg) {
        "^--debug$" { $build_type = "debug"; break }
        "^--run$"   { $run_after = $true; break }
        "^--help$"  { usage; exit 0 }
        "^--"       { bad_arg $arg }
        "^-.+" {
            foreach ($flag in $arg.Substring(1).ToCharArray()) {
                switch -CaseSensitive ($flag) {
                    "d" { $build_type = "debug" }
                    "r" { $run_after = $true }
                    "h" { usage; exit 0 }
                    default { bad_arg "-$flag" }
                }
            }
            break
        }
        default { bad_arg $arg }
    }
}

foreach ($line in Get-Content "$ROOT_DIR\glazeenv") {
    if ($line -match '^export\s+(\w+)="(.*)"$' -and -not [Environment]::GetEnvironmentVariable($Matches[1])) {
        [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2])
    }
}

$msys2_root    = if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } else { "C:\msys64" }
$toolchain_bin = "$msys2_root\ucrt64\bin"

$env:PATH = "$toolchain_bin;$env:PATH"

$cmake_build_type = if ($build_type -eq "debug") { "Debug" } else { "Release" }

cmake -G Ninja -B ".cmake_build/$build_type" -S . `
    "-DCMAKE_BUILD_TYPE=$cmake_build_type" `
    "-DCMAKE_C_COMPILER=gcc" `
    "-DCMAKE_CXX_COMPILER=g++" `
    "-DCMAKE_EXE_LINKER_FLAGS=-static-libgcc -static-libstdc++"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build ".cmake_build/$build_type"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$out_dir = "build/$env:glaze_build_id/windows/$build_type"
New-Item -ItemType Directory -Force -Path $out_dir | Out-Null

$exe_path = "$out_dir/$env:glaze_bin_name.exe"
Copy-Item ".cmake_build/$build_type/bin/glaze_executable.exe" $exe_path -Force

$pending = [System.Collections.Generic.Queue[string]]::new()
$copied  = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)

$pending.Enqueue($exe_path)

while ($pending.Count -gt 0) {
    $binary = $pending.Dequeue()

    foreach ($line in & objdump -p $binary) {
        if ($line -notmatch 'DLL Name:\s*(\S+)') {
            continue
        }

        $dll    = $Matches[1]
        $source = Join-Path $toolchain_bin $dll

        if ((Test-Path $source) -and $copied.Add($dll)) {
            Copy-Item $source $out_dir -Force
            $pending.Enqueue($source)
        }
    }
}

Write-Host "executable: $exe_path"

if ($run_after) {
    Write-Host "running.."
    & $exe_path
    Write-Host "..quit"
}
