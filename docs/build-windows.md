# Сборка и запуск под Windows (Visual Studio 2022)

Сервер отчётов собирается тем же CMake, что и на Linux: различия платформы спрятаны в
`backend/src/net.h` (Winsock вместо POSIX-сокетов, `_mkgmtime` вместо `timegm`), а зависимости
приходят из Conan. Фронтенд — это Node.js, его сборка от ОС не зависит и описана отдельно
(см. `docs/build.md`).

> Проверено на Linux (тесты зелёные); сборка MSVC **не проверена** — на машине разработки нет
> Windows. Если что-то не собирается, присылайте вывод первой ошибки: доработаем по факту.

## Что нужно

| Компонент | Версия | Проверка |
|---|---|---|
| Visual Studio 2022 | 17.4+ с нагрузкой «Разработка классических приложений на C++» | `cl` в «x64 Native Tools Command Prompt» |
| CMake | 3.20+ | `cmake --version` |
| Python | 3.8–3.11 (для Conan) | `python --version` |
| Conan | **1.66.0** (v1, не 2.x) | `conan --version` → `Conan version 1.66.0` |
| Git | любой | — |
| PostgreSQL | сервер отдельно (клиентская библиотека приедет из Conan) | `psql --version` |

```powershell
pip install "conan==1.66.0"
conan user   # один раз: Conan попросит имя, репозиторий ConanCenter по умолчанию
```

## Сборка

Из корня репозитория:

```powershell
.\scripts\build.ps1
```

Скрипт делает три шага, каждый можно повторить руками:

```powershell
# 1. зависимости в backend\build-windows\conan
conan install backend -if backend\build-windows\conan `
  -pr backend\conan\profiles\windows-msvc2022 `
  -s build_type=Release -s compiler.runtime=MD --build=missing

# 2. конфигурация (пресет из backend\CMakePresets.json)
cmake --preset windows-msvc

# 3. сборка и тесты
cmake --build --preset windows-release
ctest --preset windows-release
```

Первый `conan install` собирает drogon, openssl, libpq и libxlsxwriter из исходников — это
15–40 минут и несколько гигабайт в `%USERPROFILE%\.conan`. Дальше он берётся из кэша.

Отладочная сборка:

```powershell
.\scripts\build.ps1 -Configuration Debug
```

`Debug` требует пакетов с рантаймом `MDd` — скрипт передаёт `-s compiler.runtime=MDd`
автоматически, но их придётся собрать заново (`--build=missing`).

## Запуск

DLL зависимостей лежат рядом с конфигурацией Conan, их нужно добавить в `PATH`:

```powershell
$env:PATH = "$PWD\backend\build-windows\conan\bin;$env:PATH"

$env:REP_DB  = "postgresql://postgres:ПАРОЛЬ@192.168.0.3:32770/reporting"
$env:REP_RO_DB_USER = "reporting_ro"
$env:REP_RO_DB_PASSWORD = "ПАРОЛЬ"
$env:REP_ADMIN = "1"
$env:REP_AUTH = "1"

.\backend\build-windows\Release\rep.exe
```

Если переменные не хочется задавать каждый раз — положите их в `backend\.env` (сервер читает
файл при старте, формат `КЛЮЧ=значение`, по строке на переменную).

Проверка:

```powershell
curl.exe http://127.0.0.1:8080/api/health
# {"authMode":"session","db":true,"maxRows":50000,"schemaVersion":2,"status":"ok","version":"..."}
```

Тестовый SMTP-приёмник (для рассылки) — обычный Python:

```powershell
python -m smtpd -n -c DebuggingServer 127.0.0.1:1025
```

## Что сделано под Windows специально

| Место | Было | Стало |
|---|---|---|
| `backend/src/net.h/.cc` | POSIX-сокеты прямо в `main.cc`, `smtp.cc` | тонкий слой: `socket_t`, `closeSocket`, `setIoTimeout`, `portFree`, `timegmUtc` |
| `main.cc` | `socket/bind/close`, `::timegm` | `rep::net::portFree`, `rep::net::timegmUtc` |
| `smtp.cc` | `ssize_t`, `errno`, `::close` | `int`, `rep::net::lastError()/errorText()`, `closeSocket` |
| `subscriptions.cc` | `::timegm` | `rep::net::timegmUtc` |
| `CMakeLists.txt` | — | `ws2_32`, `WIN32_LEAN_AND_MEAN`, `NOMINMAX`, `_WIN32_WINNT=0x0A00` |
| MSVC | — | `/utf-8` (русский текст в строках), `/bigobj`, `/permissive-`, CRT `MD`/`MDd` |
| тесты | `REP_SOURCE_DIR` с обратными слэшами | `file(TO_CMAKE_PATH …)`: строка C++ не ломается на `\U` |
| `CMakePresets.json` | — | пресеты `windows-msvc` (VS 2022), `windows-msvc-ninja`, `linux-release` |

## Типичные ошибки

**`Conan version 2.x`** — проект собран на Conan v1. Поставьте рядом отдельное окружение:
`py -3.11 -m venv .venv-conan; .\.venv-conan\Scripts\activate; pip install "conan==1.66.0"`.

**`Could not find a package configuration file provided by "Drogon"`** — не выполнен
`conan install` или CMake ищет не туда. Проверьте, что `backend\build-windows\conan` содержит
`DrogonConfig.cmake`/`FindDrogon.cmake`, и что пресет `windows-msvc` использован именно с ним.

**`Не удаётся найти libpq.dll / libssl-3-x64.dll`** — не добавлен `backend\build-windows\conan\bin`
в `PATH` (или запуск из другого окна PowerShell).

**Кракозябры вместо русского текста** — сборка без `/utf-8` (флаг ставится CMake автоматически
для MSVC) или консоль в CP866: `chcp 65001` перед запуском. В PowerShell также
`[Console]::OutputEncoding = [Text.Encoding]::UTF8`.

**`LNK2038: обнаружено несоответствие для "_ITERATOR_DEBUG_LEVEL"`** — смешаны Release-пакеты с
Debug-сборкой: для `Debug` нужны `-s compiler.runtime=MDd -s build_type=Debug`.

**`порт 8080 уже занят — сервер не запущен`** — так задумано: проверка порта сработала до старта
drogon. Найдите процесс: `Get-NetTCPConnection -LocalPort 8080 | Select-Object OwningProcess`
и `Get-Process -Id <pid>`.

**Длинные пути** — Conan любит глубокие каталоги. Включите поддержку длинных путей:
`reg add HKLM\SYSTEM\CurrentControlSet\Control\FileSystem /v LongPathsEnabled /t REG_DWORD /d 1 /f`
(нужен перезапуск).
