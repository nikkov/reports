#!/usr/bin/env bash
# Полная проверка проекта. Зелёный вывод = можно принимать фазу.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
. "$ROOT/scripts/env.sh"

fail() { echo "FAIL: $*" >&2; exit 1; }
step() { echo; echo "=== $* ==="; }

step "Node.js"
if [ -f "$ROOT/frontend/package.json" ]; then
  NODE_MAJOR="$(node -p 'process.versions.node.split(".")[0]' 2>/dev/null || echo 0)"
  if [ "$NODE_MAJOR" -lt 20 ]; then
    fail "нужен Node.js >= 20 (требование Playwright), в PATH: $(node -v 2>/dev/null || echo 'node не найден'). Пример: export PATH=\"$HOME/.nvm/versions/node/v24.22.1/bin:$PATH\""
  fi
  echo "node $(node -v)"
fi

step "Conan v1"
VER="$("$ROOT/scripts/conan" --version 2>/dev/null | tail -1 | awk '{print $3}')"
case "$VER" in
  1.*) echo "conan $VER" ;;
  *) fail "нужен Conan v1, получен '$VER'" ;;
esac

step "Запрещённые коммерческие/enterprise-компоненты (ADR-10, ADR-20)"
if [ -d "$ROOT/frontend/src" ]; then
  if grep -RInE "from ['\"]vxe-table|vxe-table-pro|aggregateConfig|@vxe-ui/|ag-grid|handsontable|ag-studio|企业版" "$ROOT/frontend/src" >/dev/null 2>&1; then
    fail "найдена зависимость от коммерческого/enterprise табличного компонента"
  fi
  echo "ok"
else
  echo "skip (нет frontend/src)"
fi

step "Лицензии прямых зависимостей (DoD §5)"
if [ -f "$ROOT/scripts/licenses.mjs" ] && command -v node >/dev/null 2>&1; then
  node "$ROOT/scripts/licenses.mjs" | tail -1
else
  echo "skip (нет node или scripts/licenses.mjs)"
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

step "Frontend: покрытие engine/ (DoD ≥ 90%)"
if [ -f "$ROOT/frontend/package.json" ]; then
  ( cd "$ROOT/frontend" && npm run --silent coverage | tail -2 )
else
  echo "skip (нет frontend)"
fi

step "Frontend: build + tests"
if [ -f "$ROOT/frontend/package.json" ]; then
  (
    cd "$ROOT/frontend"
    npm run --silent build >/dev/null && echo "build ok"
    if node -e "const s=require('./package.json').scripts||{}; process.exit(s.validate?0:1)"; then
      npm run --silent validate -- ../backend/templates/*.json && echo "templates ok"
    else
      echo "skip templates (нет npm-скрипта validate)"
    fi
    if node -e "const s=require('./package.json').scripts||{}; process.exit(s.test?0:1)"; then
      npm run --silent test
    else
      echo "skip tests (нет npm-скрипта test)"
    fi
    if [ "${REP_E2E:-0}" = "1" ]; then
      npm run --silent golden
      npm run --silent e2e
    else
      echo "skip e2e (нужны запущенные backend:8080 и vite:5173; включить: REP_E2E=1 ./scripts/check.sh)"
    fi
  )
else
  echo "skip (нет frontend/package.json)"
fi

echo
echo "ALL CHECKS PASSED"
