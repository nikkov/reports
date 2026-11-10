#!/usr/bin/env bash
# Инициализация БД сервера отчётов. Ядро и примеры разделены по схемам (ADR-74):
#   reports          — ядро продукта: шаблоны, история версий, пользователи, сессии,
#                      рассылка, журнал действий;
#   reports_examples — синтетические данные демонстрационных отчётов.
#
#   ./db/init.sh                  — ядро + примеры (dev-база целиком)
#   ./db/init.sh --core-only      — только ядро, без демо-данных
#   ./db/init.sh --examples-only  — только примеры (ядро уже стоит)
#
# Файлы: db/core.sql, db/examples/schema.sql, db/examples/seed.sql, db/migrations/*.sql.
# Идемпотентно: структура создаётся через IF NOT EXISTS, примеры перезаливаются.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
. "$ROOT/scripts/env.sh"

CORE=1
EXAMPLES=1
case "${1:-}" in
  --core-only) CORE=1; EXAMPLES=0 ;;
  --examples-only) CORE=0; EXAMPLES=1 ;;
  "") ;;
  *) echo "использование: $0 [--core-only|--examples-only]" >&2; exit 2 ;;
esac

# Цель задаётся REP_DB (иначе PGDATABASE): env.sh перечитывает backend/.env,
# поэтому полагаться на унаследованный PGDATABASE нельзя — так можно случайно
# пересоздать рабочую базу вместо тестовой.
DB="${REP_DB:-${PGDATABASE:-reporting}}"
export PGDATABASE="$DB"
PSQL=(psql -v ON_ERROR_STOP=1)

RO_USER="${REP_RO_DB_USER:-reporting_ro}"

echo "== host $PGHOST:$PGPORT user $PGUSER db $DB"
echo "   ядро: $([ "$CORE" = 1 ] && echo да || echo нет), примеры: $([ "$EXAMPLES" = 1 ] && echo да || echo нет)"

exists="$(${PSQL[@]} -d postgres -tAc "SELECT 1 FROM pg_database WHERE datname = '$DB'")"
if [ -z "$exists" ]; then
  ${PSQL[@]} -d postgres -c "CREATE DATABASE \"$DB\" ENCODING 'UTF8' TEMPLATE template0"
  echo "created database $DB"
else
  echo "database $DB already exists"
fi

if [ "$CORE" = 1 ]; then
  echo "== ядро (схема reports)"
  ${PSQL[@]} -d "$DB" -q -f "$ROOT/db/core.sql"

  # миграции: идемпотентные скрипты поверх ядра — для баз, созданных до схемы reports
  for migration in "$ROOT"/db/migrations/*.sql; do
    [ -e "$migration" ] || continue
    ${PSQL[@]} -d "$DB" -q -f "$migration"
    echo "   migration: $(basename "$migration")"
  done
fi

if [ "$EXAMPLES" = 1 ]; then
  echo "== примеры (схема reports_examples)"
  ${PSQL[@]} -d "$DB" -q -f "$ROOT/db/examples/schema.sql"
  ${PSQL[@]} -d "$DB" -q -f "$ROOT/db/examples/seed.sql"
fi

# Права: приложение пишет в ядро и читает примеры; роль только-чтения видит только примеры.
if [ -n "$(${PSQL[@]} -d postgres -tAc "SELECT 1 FROM pg_roles WHERE rolname = '$RO_USER'")" ]; then
  if [ "$CORE" = 1 ]; then
    ${PSQL[@]} -d "$DB" -q -c "GRANT USAGE ON SCHEMA reports TO \"$PGUSER\"; GRANT ALL ON ALL TABLES IN SCHEMA reports TO \"$PGUSER\"; GRANT ALL ON ALL SEQUENCES IN SCHEMA reports TO \"$PGUSER\";"
  fi
  if [ "$EXAMPLES" = 1 ]; then
    ${PSQL[@]} -d "$DB" -q -c "GRANT USAGE ON SCHEMA reports_examples TO \"$PGUSER\"; GRANT SELECT ON ALL TABLES IN SCHEMA reports_examples TO \"$PGUSER\";"
    ${PSQL[@]} -d "$DB" -q -c "GRANT USAGE ON SCHEMA reports_examples TO \"$RO_USER\"; GRANT SELECT ON ALL TABLES IN SCHEMA reports_examples TO \"$RO_USER\";"
    ${PSQL[@]} -d "$DB" -q -c "ALTER DEFAULT PRIVILEGES IN SCHEMA reports_examples GRANT SELECT ON TABLES TO \"$RO_USER\";"
  fi
  echo "read-only: $RO_USER — SELECT на reports_examples"
fi

# Шаблоны из файлов в БД (ADR-21). Загрузка идёт через админский API, поэтому
# при бутстрапе без запущенного бэкенда её пропускают (REP_SKIP_TEMPLATES=1).
if [ "${REP_SKIP_TEMPLATES:-0}" = "1" ]; then
  echo "templates: пропущены (REP_SKIP_TEMPLATES=1)"
elif [ "$CORE" = 1 ] && command -v node >/dev/null 2>&1; then
  node "$ROOT/scripts/templates-push.mjs" "$ROOT"/backend/templates/*.json >/dev/null
  echo "templates: $(${PSQL[@]} -d "$DB" -tAc "SELECT count(*) FROM reports.report_templates")"
fi

if [ "$EXAMPLES" = 1 ]; then
  counts="$(${PSQL[@]} -d "$DB" -tAc "SELECT (SELECT count(*) FROM reports_examples.sales) || '/' || (SELECT count(*) FROM reports_examples.finance) || '/' || (SELECT count(*) FROM reports_examples.ops)")"
  echo "rows sales/finance/ops: $counts"
  [ "$counts" = "50000/576/540" ] || { echo "ERROR: неожиданное число строк: $counts" >&2; exit 1; }
fi
echo "OK"
