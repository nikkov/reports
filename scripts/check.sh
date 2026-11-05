#!/usr/bin/env bash
# Полная проверка проекта. Зелёный вывод = можно принимать фазу.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
. "$ROOT/scripts/env.sh"

fail() { echo "FAIL: $*" >&2; exit 1; }
step() { echo; echo "=== $* ==="; }

step "Conan v1"
VER="$("$ROOT/scripts/conan" --version 2>/dev/null | tail -1 | awk '{print $3}')"
case "$VER" in
  1.*) echo "conan $VER" ;;
  *) fail "нужен Conan v1, получен '$VER'" ;;
esac

step "Запрещённые enterprise-API vxe-table (ADR-10)"
if [ -d "$ROOT/frontend/src" ]; then
  if grep -RInE "aggregateConfig|vxe-table-pro|@vxe-ui/plugin-|企业版" "$ROOT/frontend/src" >/dev/null 2>&1; then
    fail "найдены обращения к enterprise-фичам vxe-table"
  fi
  echo "ok"
else
  echo "skip (нет frontend/src)"
fi

step "Backend: сборка и тесты"
if [ -f "$ROOT/backend/CMakeLists.txt" ]; then
  cmake -S "$ROOT/backend" -B "$ROOT/backend/build" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$ROOT/backend/build" >/dev/null
  cmake --build "$ROOT/backend/build" -j"$(nproc)" >/dev/null
  ( cd "$ROOT/backend/build" && ctest --output-on-failure )
else
  echo "skip (нет backend/CMakeLists.txt)"
fi

step "Frontend: build + tests"
if [ -f "$ROOT/frontend/package.json" ]; then
  (
    cd "$ROOT/frontend"
    npm run --silent build >/dev/null && echo "build ok"
    if node -e "const s=require('./package.json').scripts||{}; process.exit(s.test?0:1)"; then
      npm run --silent test
    else
      echo "skip tests (нет npm-скрипта test)"
    fi
  )
else
  echo "skip (нет frontend/package.json)"
fi

echo
echo "ALL CHECKS PASSED"
