#!/usr/bin/env bash
# Вторая БД для демонстрации внешнего источника (ADR-76).
#
# Показывает, что шаблон может читать данные не только из БД сервера: в блоке
# `connections` задаются адрес, база и учётная запись, а пароль берётся из переменной
# окружения (`passwordEnv`). Здесь создаётся отдельная база `reporting_ext` со схемой
# `warehouse` и ролью только-чтения `reporting_ext_ro`.
#
#   ./db/examples/external.sh            — создать/обновить базу и данные
#   ./db/examples/external.sh --dry-run  — показать, что будет создано
#
# Скрипт идемпотентен. Пароль роли генерируется один раз и пишется в backend/.env
# как REP_CONN_EXT_PASSWORD — из него его берёт сервер при подключении.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck disable=SC1091
. "$ROOT/scripts/env.sh"

ADMIN_USER="${PGADMIN_USER:-$(whoami)}"
ADMIN_HOST="${PGADMIN_HOST:-/var/run/postgresql}"
PORT="${REP_LOCAL_PORT:-5432}"
ENV_FILE="$ROOT/backend/.env"
EXT_DB="${REP_EXT_DB:-reporting_ext}"
EXT_ROLE="${REP_EXT_USER:-reporting_ext_ro}"
DRY=0
[ "${1:-}" = "--dry-run" ] && DRY=1

ADMIN=(psql -h "$ADMIN_HOST" -p "$PORT" -U "$ADMIN_USER" -v ON_ERROR_STOP=1 -q)
die() { echo "ошибка: $*" >&2; exit 1; }

command -v psql >/dev/null || die "нужен psql"

if [ "$DRY" = "1" ]; then
  echo "будет создана база $EXT_DB (владелец $ADMIN_USER), роль $EXT_ROLE (только чтение),"
  echo "схема warehouse с таблицей shipments (6 регионов × 3 месяца) и запись REP_CONN_EXT_PASSWORD в backend/.env"
  exit 0
fi

PW="$(grep -E '^REP_CONN_EXT_PASSWORD=' "$ENV_FILE" 2>/dev/null | tail -1 | cut -d= -f2- || true)"
[ -n "$PW" ] || PW="$(openssl rand -hex 16)"

echo "== внешняя база $EXT_DB на $ADMIN_HOST:$PORT"
"${ADMIN[@]}" -d postgres -c "DO \$\$ BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = '$EXT_ROLE') THEN
      CREATE ROLE \"$EXT_ROLE\" LOGIN;
    END IF;
  END \$\$;"
"${ADMIN[@]}" -d postgres -c "ALTER ROLE \"$EXT_ROLE\" PASSWORD '$PW'; ALTER ROLE \"$EXT_ROLE\" NOSUPERUSER NOCREATEDB NOCREATEROLE;"

if [ -z "$("${ADMIN[@]}" -d postgres -tAc "SELECT 1 FROM pg_database WHERE datname = '$EXT_DB'")" ]; then
  "${ADMIN[@]}" -d postgres -c "CREATE DATABASE \"$EXT_DB\" ENCODING 'UTF8' TEMPLATE template0"
  echo "   создана база $EXT_DB"
fi

PGPASSWORD="" "${ADMIN[@]}" -d "$EXT_DB" <<'SQL'
CREATE SCHEMA IF NOT EXISTS warehouse;

-- Отгрузки по регионам и месяцам: суммы сопоставимы с продажами из reports_examples,
-- чтобы в одном отчёте можно было сравнить два независимых источника.
CREATE TABLE IF NOT EXISTS warehouse.shipments (
    region        text        NOT NULL,
    month         date        NOT NULL,
    shipped_amount bigint     NOT NULL,   -- минорные единицы
    orders_count  integer     NOT NULL,
    PRIMARY KEY (region, month)
);

TRUNCATE warehouse.shipments;
INSERT INTO warehouse.shipments (region, month, shipped_amount, orders_count) VALUES
  ('Восток',  '2026-01-01', 27450000, 412),
  ('Восток',  '2026-02-01', 29100000, 438),
  ('Восток',  '2026-03-01', 31800000, 465),
  ('Запад',   '2026-01-01', 25900000, 389),
  ('Запад',   '2026-02-01', 27300000, 401),
  ('Запад',   '2026-03-01', 29700000, 427),
  ('Север',   '2026-01-01', 21400000, 305),
  ('Север',   '2026-02-01', 22600000, 318),
  ('Север',   '2026-03-01', 24150000, 339),
  ('Юг',      '2026-01-01', 23700000, 352),
  ('Юг',      '2026-02-01', 25050000, 366),
  ('Юг',      '2026-03-01', 26800000, 381),
  ('Центр',   '2026-01-01', 30200000, 447),
  ('Центр',   '2026-02-01', 31900000, 469),
  ('Центр',   '2026-03-01', 33400000, 492);
SQL

# Роль видит только схему warehouse и только читает.
"${ADMIN[@]}" -d "$EXT_DB" -c "GRANT USAGE ON SCHEMA warehouse TO \"$EXT_ROLE\";"
"${ADMIN[@]}" -d "$EXT_DB" -c "GRANT SELECT ON ALL TABLES IN SCHEMA warehouse TO \"$EXT_ROLE\";"
"${ADMIN[@]}" -d "$EXT_DB" -c "ALTER DEFAULT PRIVILEGES IN SCHEMA warehouse GRANT SELECT ON TABLES TO \"$EXT_ROLE\";"

# Пароль — только в окружении: в шаблоне остаётся имя переменной (passwordEnv)
if grep -q '^REP_CONN_EXT_PASSWORD=' "$ENV_FILE" 2>/dev/null; then
  sed -i "s|^REP_CONN_EXT_PASSWORD=.*|REP_CONN_EXT_PASSWORD=$PW|" "$ENV_FILE"
else
  printf '\n# внешний источник для примера sales-external (ADR-76)\nREP_CONN_EXT_PASSWORD=%s\n' "$PW" >> "$ENV_FILE"
fi

rows=$(PGPASSWORD="$PW" psql -h 127.0.0.1 -p "$PORT" -U "$EXT_ROLE" -d "$EXT_DB" -tAc \
  "SELECT count(*) FROM warehouse.shipments")
echo "   строк в warehouse.shipments: $rows (роль $EXT_ROLE, только чтение)"
[ "$rows" = "15" ] || die "неожиданное число строк: $rows"
echo "OK: внешний источник готов; шаблон sales-external читает его по ключу connections.ext"
