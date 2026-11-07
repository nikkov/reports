#!/usr/bin/env bash
# Создание БД reporting и загрузка синтетики. Идемпотентно.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
. "$ROOT/scripts/env.sh"

# Цель задаётся REP_DB (иначе PGDATABASE): env.sh перечитывает backend/.env,
# поэтому полагаться на унаследованный PGDATABASE нельзя — так можно случайно
# пересоздать рабочую базу вместо тестовой.
DB="${REP_DB:-${PGDATABASE:-reporting}}"
export PGDATABASE="$DB"
PSQL=(psql -v ON_ERROR_STOP=1)

echo "== host $PGHOST:$PGPORT user $PGUSER db $DB =="
echo "   внимание: таблицы отчётов в $DB будут пересозданы (schema.sql)"

exists="$(${PSQL[@]} -d postgres -tAc "SELECT 1 FROM pg_database WHERE datname = '$DB'")"
if [ -z "$exists" ]; then
  ${PSQL[@]} -d postgres -c "CREATE DATABASE \"$DB\" ENCODING 'UTF8' TEMPLATE template0"
  echo "created database $DB"
else
  echo "database $DB already exists"
fi

${PSQL[@]} -d "$DB" -q -f "$ROOT/db/schema.sql"
${PSQL[@]} -d "$DB" -q -f "$ROOT/db/seed.sql"

# миграции поверх схемы (ADR-46 и далее): идемпотентные скрипты из db/migrations
for migration in "$ROOT"/db/migrations/*.sql; do
  [ -e "$migration" ] || continue
  "${PSQL[@]}" -d "$DB" -q -f "$migration"
  echo "migration: $(basename "$migration")"
done

# шаблоны из файлов в БД (ADR-21). Загрузка идёт через админский API, поэтому
# при бутстрапе без запущенного бэкенда её пропускают (REP_SKIP_TEMPLATES=1).
if [ "${REP_SKIP_TEMPLATES:-0}" = "1" ]; then
  echo "templates: пропущены (REP_SKIP_TEMPLATES=1)"
elif command -v node >/dev/null 2>&1; then
  node "$ROOT/scripts/templates-push.mjs" "$ROOT"/backend/templates/*.json >/dev/null
  echo "templates: $(${PSQL[@]} -d "$DB" -tAc "SELECT count(*) FROM report_templates")"
fi

# Роль только-чтения (ADR-33) создаётся в кластере один раз; права выдаются на каждую БД,
# иначе на новой базе (CI, второй стенд) чтение падает с «permission denied».
RO_USER="${REP_RO_DB_USER:-reporting_ro}"
if [ -n "$(${PSQL[@]} -d postgres -tAc "SELECT 1 FROM pg_roles WHERE rolname = '$RO_USER'")" ]; then
  ${PSQL[@]} -d "$DB" -q -c "GRANT USAGE ON SCHEMA public TO \"$RO_USER\"; GRANT SELECT ON ALL TABLES IN SCHEMA public TO \"$RO_USER\";"
  ${PSQL[@]} -d "$DB" -q -c "ALTER DEFAULT PRIVILEGES IN SCHEMA public GRANT SELECT ON TABLES TO \"$RO_USER\";"
  echo "read-only: $RO_USER получает SELECT в $DB"
fi

counts="$(${PSQL[@]} -d "$DB" -tAc "SELECT (SELECT count(*) FROM sales) || '/' || (SELECT count(*) FROM finance) || '/' || (SELECT count(*) FROM ops)")"
echo "rows sales/finance/ops: $counts"

[ "$counts" = "50000/576/540" ] || { echo "ERROR: неожиданное число строк: $counts" >&2; exit 1; }
echo "OK"
