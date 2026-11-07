#!/usr/bin/env bash
# Проверки без e2e (e2e требуют запущенных сервисов и БД): сборка бэкенда, unit-тесты,
# валидация шаблонов, юнит-тесты и сборка фронтенда. Используется в CI и локально.
#   ./scripts/ci.sh              — всё перечисленное
#   REP_E2E=1 ./scripts/ci.sh    — плюс Playwright (нужны backend и preview на :8080/:5173)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
. "$ROOT/scripts/env.sh"

step() { echo; echo "=== $* ==="; }

step "Бэкенд: зависимости"
cd "$ROOT/backend"
if [ ! -f build/DrogonConfig.cmake ]; then
  ../scripts/conan install . -if build --build missing -s build_type=Release -s compiler.libcxx=libstdc++11
else
  echo "зависимости уже установлены"
fi

step "Бэкенд: сборка"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PWD/build" >/dev/null
cmake --build build -j"$(nproc)"

step "Бэкенд: ctest"
( cd build && ctest --output-on-failure )

step "Фронтенд: зависимости"
cd "$ROOT/frontend"
npm ci --no-audit --no-fund 2>/dev/null || npm install --no-audit --no-fund

step "Фронтенд: схема и шаблоны"
npm run --silent validate -- ../backend/templates/*.json

step "Фронтенд: юнит-тесты"
npx vitest run

step "Фронтенд: сборка"
npm run --silent build

if [ "${REP_E2E:-0}" = "1" ]; then
  step "Фронтенд: e2e"
  npx playwright test
else
  echo
  echo "e2e пропущены: нужны запущенные backend и preview (./scripts/demo.sh), затем REP_E2E=1 ./scripts/ci.sh"
fi

echo
echo "CI: все проверки пройдены"
