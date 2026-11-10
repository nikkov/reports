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

step "Реестр ADR (docs/adr.md из PLAN.md)"
if command -v node >/dev/null 2>&1 && [ -f "$ROOT/scripts/adr.mjs" ]; then
  node "$ROOT/scripts/adr.mjs" --check || fail "docs/adr.md устарел: node scripts/adr.mjs"
else
  echo "skip (нет node или scripts/adr.mjs)"
fi

step "Версия (VERSION, README, CHANGELOG, package.json)"
"$ROOT/scripts/version.sh" --check || fail "версия расходится между файлами"

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

step "Скрипты: синтаксис bash"
bad=0
for s in scripts/*.sh db/*.sh db/examples/*.sh; do
  [ -e "$s" ] || continue
  if ! bash -n "$s" 2>/dev/null; then
    echo "FAIL: синтаксис $s" >&2
    bash -n "$s" || true
    bad=1
  fi
done
[ "$bad" = "0" ] || exit 1
echo "синтаксис скриптов: ok"

step "Backend: сборка и тесты"
if [ -f "$ROOT/backend/CMakeLists.txt" ]; then
  cmake -S "$ROOT/backend" -B "$ROOT/backend/build" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$ROOT/backend/build" >/dev/null
  cmake --build "$ROOT/backend/build" -j"$(nproc)" >/dev/null
  ( cd "$ROOT/backend/build" && ctest --output-on-failure )
else
  echo "skip (нет backend/CMakeLists.txt)"
fi

# Внешний источник для примера sales-external (ADR-76): отдельная БД warehouse.
# Нужен e2e-проверке «данные из другой БД»; если прав на создание баз нет — тест
# упадёт с понятным сообщением, а не тихо пропустит проверку.
if [ "${REP_E2E:-0}" = "1" ] && [ -x "$ROOT/db/examples/external.sh" ]; then
  if "$ROOT/db/examples/external.sh" >/dev/null 2>&1; then
    echo "внешний источник (reporting_ext/warehouse) готов"
  else
    echo "ВНИМАНИЕ: внешний источник не создан — e2e нужен db/examples/external.sh"
  fi
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
      # без `&&`: его левая часть не подпадает под set -e, и ошибка шаблона
      # раньше молча проглатывалась — так схема разошлась с форматом (ADR-55)
      if npm run --silent validate -- ../backend/templates/*.json; then
        echo "templates ok"
      else
        echo "FAIL: шаблоны не проходят валидацию (схема или шаблон)" >&2
        exit 1
      fi
    else
      echo "skip templates (нет npm-скрипта validate)"
    fi
    if node -e "const s=require('./package.json').scripts||{}; process.exit(s.test?0:1)"; then
      npm run --silent test
    else
      echo "skip tests (нет npm-скрипта test)"
    fi
    if [ "${REP_E2E:-0}" = "1" ]; then
      # сервер отдаёт страницы сам (ADR-79): сборка должна лежать рядом с бинарником
      rm -rf "$ROOT/backend/build/web"
      cp -a "$ROOT/frontend/dist" "$ROOT/backend/build/web"
      npm run --silent golden
      npm run --silent e2e
    else
      echo "skip e2e (нужен backend:8080 — он же отдаёт статику; включить: REP_E2E=1 ./scripts/check.sh)"
    fi
  )
else
  echo "skip (нет frontend/package.json)"
fi

# e2e создаёт и удаляет свои отчёты, а снимки версий остаются (FK между историей и
# шаблоном нет по ADR-26). Убираем сирот сразу после прогона, чтобы мусор не копился.
# Без доступной БД шаг пропускается — check.sh должен работать и на голой машине.
if command -v psql >/dev/null 2>&1; then
  # shellcheck disable=SC1091
  if . "$(dirname "$0")/env.sh" >/dev/null 2>&1 && psql -tAc "SELECT 1" >/dev/null 2>&1; then
    echo "=== Чистка сирот в БД (db-cleanup.sh) ==="
    "$(dirname "$0")/db-cleanup.sh" | tail -1
  else
    echo "=== Чистка БД пропущена: база недоступна ==="
  fi
fi

echo
echo "ALL CHECKS PASSED"
