#!/usr/bin/env bash
# Создание БД reporting и загрузка синтетики. Идемпотентно.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
. "$ROOT/scripts/env.sh"

DB="${PGDATABASE:-reporting}"
PSQL=(psql -v ON_ERROR_STOP=1)

echo "== host $PGHOST:$PGPORT user $PGUSER db $DB =="

exists="$(${PSQL[@]} -d postgres -tAc "SELECT 1 FROM pg_database WHERE datname = '$DB'")"
if [ -z "$exists" ]; then
  ${PSQL[@]} -d postgres -c "CREATE DATABASE \"$DB\" ENCODING 'UTF8' TEMPLATE template0"
  echo "created database $DB"
else
  echo "database $DB already exists"
fi

${PSQL[@]} -d "$DB" -q -f "$ROOT/db/schema.sql"
${PSQL[@]} -d "$DB" -q -f "$ROOT/db/seed.sql"

# шаблоны из файлов в БД (ADR-21)
if command -v node >/dev/null 2>&1; then
  node "$ROOT/scripts/templates-push.mjs" "$ROOT"/backend/templates/*.json >/dev/null
  echo "templates: $(${PSQL[@]} -d "$DB" -tAc "SELECT count(*) FROM report_templates")"
fi

counts="$(${PSQL[@]} -d "$DB" -tAc "SELECT (SELECT count(*) FROM sales) || '/' || (SELECT count(*) FROM finance) || '/' || (SELECT count(*) FROM ops)")"
echo "rows sales/finance/ops: $counts"

[ "$counts" = "50000/576/540" ] || { echo "ERROR: неожиданное число строк: $counts" >&2; exit 1; }
echo "OK"
