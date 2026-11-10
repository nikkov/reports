# Сборка сервера отчётов под Windows (Visual Studio 2022).
#
#   .\scripts\build.ps1                     # Release + тесты
#   .\scripts\build.ps1 -Configuration Debug
#   .\scripts\build.ps1 -SkipTests           # только сборка
#
# Скрипт ничего не скачивает сам: он проверяет, что Conan v1 и CMake на месте,
# ставит зависимости drogon/catch2/libxlsxwriter и собирает backend.

param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [switch]$SkipTests,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$backend = Join-Path $root 'backend'
$buildDir = Join-Path $backend 'build-windows'
$conanDir = Join-Path $buildDir 'conan'
$profile = Join-Path $backend 'conan\profiles\windows-msvc2022'

function Assert-Command([string]$name, [string]$hint) {
    if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
        throw "Не найден '$name'. $hint"
    }
}

Assert-Command 'conan' 'Установите Conan v1: pip install "conan==1.66.0"'
Assert-Command 'cmake' 'Установите CMake 3.20+ и добавьте его в PATH (или запустите из Developer PowerShell).'

# Проект собирается Conan v1: 2.x по-другому читает профили и падает на [options].
$conanVersion = (& conan --version) 2>&1 | Out-String
if ($conanVersion -notmatch 'Conan version 1\.') {
    throw "Нужен Conan 1.x, найден: $($conanVersion.Trim()). Установите: pip install `"conan==1.66.0`""
}

if (-not (Test-Path $profile)) {
    throw "Нет профиля Conan: $profile"
}

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "Удаляю $buildDir"
    Remove-Item -Recurse -Force $buildDir
}

# Debug-сборка требует пакетов с MDd: рантайм передаём и в профиль, и в install.
$runtime = if ($Configuration -eq 'Debug') { 'MDd' } else { 'MD' }

Write-Host "== Conan install ($Configuration, runtime $runtime)"
New-Item -ItemType Directory -Force -Path $conanDir | Out-Null
& conan install $backend -if $conanDir -pr $profile `
    -s "build_type=$Configuration" `
    -s "compiler.runtime=$runtime" `
    --build=missing
if ($LASTEXITCODE -ne 0) { throw "conan install завершился с кодом $LASTEXITCODE" }

Write-Host '== CMake configure'
& cmake --preset windows-msvc
if ($LASTEXITCODE -ne 0) { throw "cmake --preset завершился с кодом $LASTEXITCODE" }

$buildPreset = "windows-$($Configuration.ToLower())"
Write-Host "== CMake build ($buildPreset)"
& cmake --build --preset $buildPreset
if ($LASTEXITCODE -ne 0) { throw "cmake --build завершился с кодом $LASTEXITCODE" }

if (-not $SkipTests) {
    Write-Host '== CTest'
    & ctest --preset $buildPreset
    if ($LASTEXITCODE -ne 0) { throw "тесты упали (код $LASTEXITCODE)" }
}

Write-Host ''
Write-Host "Готово. Сервер: $buildDir\$Configuration\rep.exe"
Write-Host "DLL зависимостей лежат в: $conanDir\bin"
Write-Host "Запуск (в этом же окне):"
Write-Host "  `$env:PATH = '$conanDir\bin;' + `$env:PATH"
Write-Host "  .\backend\build-windows\$Configuration\rep.exe"
