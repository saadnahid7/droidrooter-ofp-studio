param([ValidateSet('Release','Debug')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    cmake -S . -B build -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration"
    if ($LASTEXITCODE) { throw 'CMake configuration failed' }
    cmake --build build
    if ($LASTEXITCODE) { throw 'Build failed' }
} finally { Pop-Location }
