#!/usr/bin/env bash
# Роль только-чтения для данных отчётов (ADR-33).
#   ./scripts/db-ro.sh            — создать роль и выдать SELECT, пароль из backend/.env
#   ./scripts/db-ro.sh --rotate   — сменить пароль и дописать его в backend/.env
#
# Пароль хранится в backend/.env (файл в git не попадает). Скрипт печатает только факт,
# а не сам пароль: строки в логах терминала не оставляем.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
. "$ROOT/scripts/env.sh"

ENV_FILE="$ROOT/backend/.env"
RO_USER="${REP_RO_DB_USER:-reporting_ro}"
ROWS="INSERT INTO regions(id, name) VALUES (99991, 'should-fail')"

rotate=false
[ "${1:-}" = "--rotate" ] && rotate=true

current="$(grep -E '^REP_RO_DB_PASSWORD=' "$ENV_FILE" 2>/dev/null | tail -1 | cut -d= -f2- || true)"
if [ -z "$current" ] || [ "$rotate" = true ]; then
  current="$(openssl rand -hex 16)"
  touch "$ENV_FILE"
  if grep -qE '^REP_RO_DB_PASSWORD=' "$ENV_FILE"; then
    tmp="$(mktemp)"
    sed "s|^REP_RO_DB_PASSWORD=.*|REP_RO_DB_PASSWORD=$current|" "$ENV_FILE" >"$tmp"
    mv "$tmp" "$ENV_FILE"
  else
    printf '\n# роль только-чтения (ADR-33)\nREP_RO_DB_USER=%s\nREP_RO_DB_PASSWORD=%s\n' "$RO_USER" "$current" >>"$ENV_FILE"
  fi
  echo "пароль роли $RO_USER обновлён в backend/.env"
fi

exists="$(psql -tAc "SELECT 1 FROM pg_roles WHERE rolname = '$RO_USER'")"
if [ -z "$exists" ]; then
  psql -v ON_ERROR_STOP=1 -c "CREATE ROLE $RO_USER LOGIN PASSWORD '$current'"
  echo "роль $RO_USER создана"
else
  psql -v ON_ERROR_STOP=1 -c "ALTER ROLE $RO_USER LOGIN PASSWORD '$current'"
  echo "роль $RO_USER обновлена"
fi

psql -v ON_ERROR_STOP=1 -f "$ROOT/db/roles.sql" >/dev/null
echo "права: SELECT на схему public, запись запрещена"

# --- проверки на живом соединении -----------------------------------------
set +e
ok_read="$(psql "host=${PGHOST} port=${PGPORT} user=${RO_USER} password=${current} dbname=${PGDATABASE}" \
  -tAc "SELECT count(*) FROM sales" 2>&1)"
set -e
if [ -z "$ok_read" ] || ! [[ "$ok_read" =~ ^[0-9]+$ ]]; then
  echo "ОШИБКА: роль не читает sales: $ok_read" >&2
  exit 1
fi
echo "чтение sales: $ok_read строк"

set +e
write_out="$(psql "host=${PGHOST} port=${PGPORT} user=${RO_USER} password=${current} dbname=${PGDATABASE}" \
  -c "$ROWS" 2>&1)"
set -e
if echo "$write_out" | grep -qi "read-only\|permission denied\|только для чтения"; then
  echo "запись запрещена: $(echo "$write_out" | head -1)"
else
  echo "ОШИБКА: роль смогла писать в базу: $write_out" >&2
  exit 1
fi
