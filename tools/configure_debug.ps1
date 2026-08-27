$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $repoRoot 'build-debug'
$compilerMetadata = Get-ChildItem (Join-Path $buildRoot 'CMakeFiles') -Filter 'CMakeCXXCompiler.cmake' -File -Recurse -ErrorAction SilentlyContinue |
    Select-Object -First 1

$needsFreshConfigure = $false
if (-not $compilerMetadata) {
    $needsFreshConfigure = Test-Path (Join-Path $buildRoot 'CMakeCache.txt')
} else {
    $metadataText = Get-Content -LiteralPath $compilerMetadata.FullName -Raw
    $needsFreshConfigure =
        $metadataText -match 'set\(CMAKE_CXX_COMPILE_FEATURES\s*""\)' -or
        $metadataText -match 'set\(CMAKE_CXX_ABI_COMPILED\s*\)' -or
        $metadataText -match 'set\(CMAKE_CXX_COMPILER_WORKS\s*\)'
}

if ($needsFreshConfigure) {
    Write-Host 'Incomplete CMake compiler metadata detected; using --fresh.'
    & cmake --fresh --preset debug
} else {
    & cmake --preset debug
}

exit $LASTEXITCODE
