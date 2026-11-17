param([string]$BuildDir = "build-host", [string]$VisualStudioRoot = "", [switch]$ConfigureOnly, [switch]$Asan)
$ErrorActionPreference = "Stop"
$env:VSLANG = "1033"
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if ($VisualStudioRoot) { $vsRoot = $VisualStudioRoot }
elseif (Test-Path -LiteralPath $vswhere) { $vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath }
else { throw 'Pass -VisualStudioRoot with the Visual Studio C++ installation directory' }
$cmakeBin = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$ninjaBin = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
$msvcRoot = (Get-ChildItem (Join-Path $vsRoot 'VC\Tools\MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
$sdkRoot = 'C:\Program Files (x86)\Windows Kits\10'
$sdkVersion = (Get-ChildItem (Join-Path $sdkRoot 'Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
$env:PATH = "$cmakeBin;$ninjaBin;$msvcRoot\bin\Hostx64\x64;$sdkRoot\bin\$sdkVersion\x64;$env:PATH"
$env:INCLUDE = "$msvcRoot\include;$sdkRoot\Include\$sdkVersion\ucrt;$sdkRoot\Include\$sdkVersion\shared;$sdkRoot\Include\$sdkVersion\um"
$env:LIB = "$msvcRoot\lib\x64;$sdkRoot\Lib\$sdkVersion\ucrt\x64;$sdkRoot\Lib\$sdkVersion\um\x64"
$asanValue = if ($Asan) { "ON" } else { "OFF" }
cmake -S . -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_RK_DEMO=OFF "-DENABLE_ASAN=$asanValue"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($ConfigureOnly) { exit 0 }
cmake --build $BuildDir --clean-first --parallel 4
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($Asan) {
    $asanRuntimeFound = $false
    foreach ($asanDll in @('clang_rt.asan_dynamic-x86_64.dll', 'clang_rt.asan_dbg_dynamic-x86_64.dll')) {
        $asanSource = Join-Path $msvcRoot "bin\Hostx64\x64\$asanDll"
        if (Test-Path -LiteralPath $asanSource) {
            Copy-Item -LiteralPath $asanSource -Destination (Join-Path $BuildDir $asanDll)
            $asanRuntimeFound = $true
        }
    }
    if (-not $asanRuntimeFound) { throw 'MSVC AddressSanitizer runtime DLL is missing from the selected toolchain' }
}
ctest --test-dir $BuildDir --output-on-failure
exit $LASTEXITCODE
