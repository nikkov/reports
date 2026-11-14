#!/usr/bin/env bash
# Статическая проверка готовности к сборке под Windows/MSVC (ADR-116).
# Собирать под MSVC на Linux нельзя, но можно гарантировать, что платформенные
# вещи идут через обёртки, а не раскиданы по коду: тогда Windows-сборка не
# спотыкается о POSIX-вызовы, которых там нет.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/backend/src"
fail=0
ok() { echo "  ok   $*"; }
bad() { echo "  FAIL $*" >&2; fail=1; }

echo "Windows/MSVC: платформенные обёртки"

# 1. Платформенно-зависимый код разрешён только в этих файлах: они содержат
#    #ifdef _WIN32 и подставляют Winsock, _putenv_s, _mkgmtime, gmtime_s.
ALLOWED="net.cc net.h env.cc logging.cc main.cc"
POSIX="unistd\.h|sys/socket\.h|arpa/inet\.h|netinet/in\.h|sys/time\.h|pthread\.h|execinfo\.h|sys/wait\.h"
raw=$(grep -rEn "$POSIX|\btimegm\(|\bgmtime_r\(|\blocaltime_r\(|\bstrptime\(|\bsetenv\(|\bpopen\(|\bdlopen\(" "$SRC" 2>/dev/null || true)
hits=""
while IFS= read -r line; do
  [ -z "$line" ] && continue
  base=$(basename "${line%%:*}")
  case " $ALLOWED " in
    *" $base "*) ;;
    *) hits+="$line"$'\n' ;;
  esac
done <<< "$raw"

if [ -n "$hits" ]; then
  bad "POSIX-вызовы вне платформенных обёрток (положите их в net.h/env.cc/logging.cc):"
  echo "$hits" | sed 's/^/       /' >&2
else
  ok "POSIX-вызовы только в net.{h,cc}, env.cc, logging.cc, main.cc"
fi

# 2. Каждый разрешённый файл должен действительно содержать ветку под Windows.
for f in "$SRC/net.cc" "$SRC/net.h" "$SRC/env.cc" "$SRC/logging.cc"; do
  if grep -q "_WIN32" "$f"; then ok "_WIN32-ветка: $(basename "$f")"; else bad "нет _WIN32-ветки: $(basename "$f")"; fi
done

# 3. Зависимости и линковка: Winsock подключается, только когда WIN32.
if grep -q "ws2_32" "$ROOT/backend/CMakeLists.txt"; then ok "ws2_32 в CMakeLists"; else bad "ws2_32 не подключён в CMakeLists"; fi
if grep -q "WIN32_LEAN_AND_MEAN" "$ROOT/backend/CMakeLists.txt"; then ok "WIN32_LEAN_AND_MEAN/NOMINMAX"; else bad "нет WIN32_LEAN_AND_MEAN"; fi

# 4. Инструменты Windows-сборки на месте: профиль Conan и скрипт PowerShell.
[ -f "$ROOT/backend/conan/profiles/windows-msvc2022" ] && ok "профиль conan windows-msvc2022" || bad "нет профиля conan windows-msvc2022"
[ -f "$ROOT/scripts/build.ps1" ] && ok "scripts/build.ps1" || bad "нет scripts/build.ps1"
[ -f "$ROOT/backend/CMakePresets.json" ] && ok "CMakePresets.json" || bad "нет CMakePresets.json"

# 5. Диагностика падений — только POSIX (execinfo.h), значит под Windows её нет совсем:
#    обработчик обязан быть внутри #if !defined(_WIN32), иначе MSVC не соберётся.
if grep -q "#if !defined(_WIN32)" "$SRC/main.cc" && grep -q "execinfo.h" "$SRC/main.cc"; then
  ok "crashHandler внутри #if !defined(_WIN32)"
else
  bad "execinfo.h без #if !defined(_WIN32) в main.cc"
fi

echo
if [ "$fail" -ne 0 ]; then
  echo "Windows-совместимость: проблемы выше" >&2
  exit 1
fi
echo "Windows-совместимость: статические проверки пройдены"
