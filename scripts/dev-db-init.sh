#!/usr/bin/env bash
# Локальная dev-БД: PostgreSQL на 127.0.0.1:5432 вместо удалённого стенда.
#
#   ./scripts/dev-db-init.sh              — накатить схему, синтетику, пользователей, шаблоны
#   ./scripts/dev-db-init.sh --no-data    — только роли, базы и схема (без seed)
#   PGADMIN_USER=postgres ./scripts/dev-db-init.sh   — если в кластере нет вашей роли
#
# Скрипт идемпотентен: роли и базы создаются один раз, пароли переиспользуются из
# backend/.env, шаблоны и пользователи обновляются. Структуру и данные создаёт
# db/init.sh (ядро — схема reports, примеры — reports_examples, ADR-74): здесь только
# роли, базы, права и .env, чтобы не держать два источника правды.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
. "$ROOT/scripts/env.sh"

ADMIN_USER="${PGADMIN_USER:-$(whoami)}"
ADMIN_HOST="${PGADMIN_HOST:-/var/run/postgresql}"
PORT="${REP_LOCAL_PORT:-5432}"
ENV_FILE="$ROOT/backend/.env"
APP_USER="rep_app"
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
APP_PW="$(read_env PGPASSWORD)"
[ -n "$APP_PW" ] || APP_PW="$(openssl rand -hex 16)"
RO_PW="$(read_env REP_RO_DB_PASSWORD)"
[ -n "$RO_PW" ] || RO_PW="$(openssl rand -hex 16)"

# Роли: CREATE IF NOT EXISTS в PostgreSQL нет, поэтому через DO-блок.
for role in "$APP_USER" "$RO_USER"; do
  "${ADMIN[@]}" -d postgres -c "DO \$\$ BEGIN
      IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = '$role') THEN
        CREATE ROLE \"$role\" LOGIN;
      END IF;
    END \$\$;"
done

# Пароли задаём всегда: так .env остаётся единственным источником истины.
"${ADMIN[@]}" -d postgres -c "ALTER ROLE \"$APP_USER\" PASSWORD '$APP_PW'; ALTER ROLE \"$RO_USER\" PASSWORD '$RO_PW';"

for db in reporting reporting_ci; do
  if [ -z "$("${ADMIN[@]}" -d postgres -tAc "SELECT 1 FROM pg_database WHERE datname = '$db'")" ]; then
    "${ADMIN[@]}" -d postgres -c "CREATE DATABASE \"$db\" OWNER \"$APP_USER\" ENCODING 'UTF8' TEMPLATE template0"
    echo "   создана база $db"
  fi
done

# Дальше работаем под приложением: таблицы должны принадлежать ему, иначе
# отчёты упадут на «permission denied for table sales».
export PGHOST=127.0.0.1 PGPORT="$PORT" PGUSER="$APP_USER" PGPASSWORD="$APP_PW"
APP=(psql -h 127.0.0.1 -p "$PORT" -U "$APP_USER" -v ON_ERROR_STOP=1 -q)

# backend/.env: переключаем приложение на локальный кластер
cat > "$ENV_FILE" <<ENV
# Подключение к dev-БД. Скопировать в backend/.env (в git не попадает).
PGHOST=127.0.0.1
PGPORT=$PORT
PGUSER=$APP_USER
PGPASSWORD=$APP_PW
PGDATABASE=reporting

# роль только-чтения (ADR-33)
REP_RO_DB_USER=$RO_USER
REP_RO_DB_PASSWORD=$RO_PW
ENV
chmod 600 "$ENV_FILE"
echo "== backend/.env переключён на 127.0.0.1:$PORT (пароли не печатаются)"

# Структуру и данные создаёт db/init.sh: ядро — схема reports, примеры — reports_examples
# (ADR-74). Дублировать DDL здесь нельзя: два источника правды разъезжаются.
for db in reporting reporting_ci; do
  echo "== схема и данные в $db"
  REP_DB="$db" REP_SKIP_TEMPLATES=1 "$ROOT/db/init.sh" $([ "$SEED" = "1" ] || echo --core-only) >/dev/null
done


# Пользователи админки (ADR-34): пароли дописываются в тот же .env
REP_AUTH=1 PGDATABASE=reporting "$ROOT/scripts/auth-init.sh" >/dev/null

if [ "$SEED" = "1" ]; then
  counts=$("${APP[@]}" -d reporting -tAc \
    "SELECT (SELECT count(*) FROM reports_examples.sales) || '/' || (SELECT count(*) FROM reports_examples.finance) || '/' || (SELECT count(*) FROM reports_examples.ops)")
  echo "   строки sales/finance/ops: $counts"
  [ "$counts" = "50000/576/540" ] || die "неожиданное число строк: $counts"
fi
echo "OK: база reporting готова. Запуск сервера: ./scripts/demo.sh (или backend/build/rep)"
echo "    шаблоны: node scripts/templates-push.mjs backend/templates/*.json (при запущенном сервере)"
