#!/usr/bin/env bash
# Локальная dev-БД: PostgreSQL на 127.0.0.1:5432 вместо удалённого стенда.
#
#   ./scripts/dev-db-init.sh              — роли, базы, схема, синтетика, пользователи, .env
#   ./scripts/dev-db-init.sh --no-data    — только роли, базы и схема (без seed)
#   PGADMIN_USER=postgres ./scripts/dev-db-init.sh   — если в кластере нет вашей роли
#
# Три роли (ADR-75):
#   reporting_owner — владелец схем и таблиц: DDL, миграции, развёртывание;
#   reporting_srv   — под ней работает сам сервер: DML в ядре, чтение примеров;
#   reporting_ro    — только чтение данных отчётов (reports_examples).
# Суперпользователь нужен лишь на этом шаге (создание ролей и баз), в эксплуатации — нет.
#
# Скрипт идемпотентен: пароли переиспользуются из backend/.env, структуру и данные
# создаёт db/init.sh (ядро — reports, примеры — reports_examples, ADR-74).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
. "$ROOT/scripts/env.sh"

ADMIN_USER="${PGADMIN_USER:-$(whoami)}"
ADMIN_HOST="${PGADMIN_HOST:-/var/run/postgresql}"
PORT="${REP_LOCAL_PORT:-5432}"
ENV_FILE="$ROOT/backend/.env"

OWNER_USER="${REP_OWNER_DB_USER:-reporting_owner}"
SRV_USER="${REP_SRV_DB_USER:-reporting_srv}"
RO_USER="${REP_RO_DB_USER:-reporting_ro}"

SEED=1
[ "${1:-}" = "--no-data" ] && SEED=0

ADMIN=(psql -h "$ADMIN_HOST" -p "$PORT" -U "$ADMIN_USER" -v ON_ERROR_STOP=1 -q)
die() { echo "ошибка: $*" >&2; exit 1; }

command -v psql >/dev/null || die "нужен psql (пакет postgresql-client)"
command -v pg_isready >/dev/null && pg_isready -h "$ADMIN_HOST" -p "$PORT" >/dev/null \
  || die "кластер PostgreSQL не отвечает на $ADMIN_HOST:$PORT — запустите его"

echo "== локальный кластер $ADMIN_HOST:$PORT (админ: $ADMIN_USER)"

# Пароли: переиспользуем из .env, иначе генерируем один раз и записываем туда же.
read_env() { [ -f "$ENV_FILE" ] && sed -n "s/^$1=//p" "$ENV_FILE" | head -1 || true; }
OWNER_PW="$(read_env REP_OWNER_DB_PASSWORD)"
[ -n "$OWNER_PW" ] || OWNER_PW="$(openssl rand -hex 16)"
SRV_PW="$(read_env PGPASSWORD)"
[ -n "$SRV_PW" ] || SRV_PW="$(openssl rand -hex 16)"
RO_PW="$(read_env REP_RO_DB_PASSWORD)"
[ -n "$RO_PW" ] || RO_PW="$(openssl rand -hex 16)"

# Роли: CREATE IF NOT EXISTS в PostgreSQL нет, поэтому через DO-блок.
for role in "$OWNER_USER" "$SRV_USER" "$RO_USER"; do
  "${ADMIN[@]}" -d postgres -c "DO \$\$ BEGIN
      IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = '$role') THEN
        CREATE ROLE \"$role\" LOGIN;
      END IF;
    END \$\$;"
done

# Пароли задаём всегда: так .env остаётся единственным источником истины.
"${ADMIN[@]}" -d postgres -c "ALTER ROLE \"$OWNER_USER\" PASSWORD '$OWNER_PW';"
"${ADMIN[@]}" -d postgres -c "ALTER ROLE \"$SRV_USER\" PASSWORD '$SRV_PW';"
"${ADMIN[@]}" -d postgres -c "ALTER ROLE \"$RO_USER\" PASSWORD '$RO_PW';"
# суперпользователь нужен только для установки: эксплуатационные роли — обычные
"${ADMIN[@]}" -d postgres -c "ALTER ROLE \"$OWNER_USER\" NOSUPERUSER NOCREATEDB NOCREATEROLE;"
"${ADMIN[@]}" -d postgres -c "ALTER ROLE \"$SRV_USER\" NOSUPERUSER NOCREATEDB NOCREATEROLE;"
"${ADMIN[@]}" -d postgres -c "ALTER ROLE \"$RO_USER\" NOSUPERUSER NOCREATEDB NOCREATEROLE;"

for db in reporting reporting_ci; do
  if [ -z "$("${ADMIN[@]}" -d postgres -tAc "SELECT 1 FROM pg_database WHERE datname = '$db'")" ]; then
    "${ADMIN[@]}" -d postgres -c "CREATE DATABASE \"$db\" OWNER \"$OWNER_USER\" ENCODING 'UTF8' TEMPLATE template0"
    echo "   создана база $db (владелец $OWNER_USER)"
  fi
done

# backend/.env: сервер работает под reporting_srv, пароль владельца — только для миграций
cat > "$ENV_FILE" <<ENV
# Подключение к dev-БД. Скопировать в backend/.env (в git не попадает).
PGHOST=127.0.0.1
PGPORT=$PORT
PGUSER=$SRV_USER
PGPASSWORD=$SRV_PW
PGDATABASE=reporting

# схемы ядра и примеров (ADR-74)
REP_DB_SCHEMA=reports
REP_RO_DB_SCHEMA=reports_examples

# владелец объектов: только для миграций и развёртывания, сервер им не ходит
REP_OWNER_DB_USER=$OWNER_USER
REP_OWNER_DB_PASSWORD=$OWNER_PW

# роль только-чтения (ADR-33)
REP_RO_DB_USER=$RO_USER
REP_RO_DB_PASSWORD=$RO_PW
ENV
chmod 600 "$ENV_FILE"
echo "== backend/.env: сервер под $SRV_USER, владелец $OWNER_USER (для миграций), зритель $RO_USER"

# Структуру и данные создаёт db/init.sh под владельцем: DDL — только его право.
for db in reporting reporting_ci; do
  echo "== схема и данные в $db"
  REP_DB="$db" REP_SKIP_TEMPLATES=1 "$ROOT/db/init.sh" $([ "$SEED" = "1" ] || echo --core-only) >/dev/null
done

# Пользователи админки (ADR-34): DML под ролью сервера, пароли дописываются в .env
REP_AUTH=1 PGDATABASE=reporting "$ROOT/scripts/auth-init.sh" >/dev/null

if [ "$SEED" = "1" ]; then
  counts=$(PGPASSWORD="$SRV_PW" psql -h 127.0.0.1 -p "$PORT" -U "$SRV_USER" -d reporting -tAc \
    "SELECT (SELECT count(*) FROM reports_examples.sales) || '/' || (SELECT count(*) FROM reports_examples.finance) || '/' || (SELECT count(*) FROM reports_examples.ops)")
  echo "   строки sales/finance/ops: $counts"
  [ "$counts" = "50000/576/540" ] || die "неожиданное число строк: $counts"
fi
echo "OK: база reporting готова. Сервер ходит под $SRV_USER, отчёты читает $RO_USER."
echo "    запуск: ./scripts/demo.sh (или backend/build/rep); шаблоны: node scripts/templates-push.mjs"
