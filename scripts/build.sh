#!/usr/bin/env bash
# Полная сборка проекта для локального запуска/демонстрации.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
. "$ROOT/scripts/env.sh"

step() { echo; echo "=== $* ==="; }

step "Conan v1"
"$ROOT/scripts/conan" --version | tail -1

step "Бэкенд: зависимости"
cd "$ROOT/backend"
if [ ! -f build/DrogonConfig.cmake ]; then
  ../scripts/conan install . -if build --build missing -s build_type=Release -s compiler.libcxx=libstdc++11
else
  echo "зависимости уже установлены (build/DrogonConfig.cmake)"
fi

step "Бэкенд: сборка"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PWD/build" >/dev/null
cmake --build build -j"$(nproc)"

step "Бэкенд: тесты"
( cd build && ctest --output-on-failure )

step "Фронтенд: зависимости и сборка"
cd "$ROOT/frontend"
npm ci --no-audit --no-fund 2>/dev/null || npm install --no-audit --no-fund
npm run --silent validate -- ../backend/templates/*.json
npm run --silent build

echo
echo "Готово. Запуск демонстрации: ./scripts/demo.sh"
