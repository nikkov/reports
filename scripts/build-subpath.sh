#!/usr/bin/env bash
# Сборка фронтенда под префикс приложения (ADR-53), не затирая обычный dist.
#   ./scripts/build-subpath.sh                 # /reports/ и /reports/api
#   REP_BASE=/portal/ VITE_API_BASE=/portal ./scripts/build-subpath.sh
set -euo pipefail
cd "$(dirname "$0")/.."

REP_BASE="${REP_BASE:-/reports/}"
VITE_API_BASE="${VITE_API_BASE:-/reports}"
OUT="${OUT:-frontend/dist-reports}"

case "$REP_BASE" in
  */) ;;
  *) REP_BASE="$REP_BASE/" ;;
esac

echo "сборка под префикс: base=$REP_BASE api=$VITE_API_BASE → $OUT"
cd frontend
REP_BASE="$REP_BASE" VITE_API_BASE="$VITE_API_BASE" npx vite build --outDir ../"$OUT" --emptyOutDir
echo "готово: $(grep -o 'src="[^"]*"' ../"$OUT"/index.html | head -1)"
