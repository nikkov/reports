#!/usr/bin/env bash
# Golden-сверка агрегатов движка с эталонным SQL (задача 2a.7).
# Требует запущенного бэкенда (по умолчанию :8080) и доступа к БД.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
. "$ROOT/scripts/env.sh"

cd "$ROOT/frontend"
exec node scripts/golden.mjs "$@"
