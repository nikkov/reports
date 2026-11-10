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

# GRANT'ы может выдавать владелец объектов (ADR-75): сервер под reporting_srv на это
# не имеет права. Для этих шагов подключаемся владельцем, если его пароль задан.
DDL_AS=()
if [ -n "${REP_OWNER_DB_USER:-}" ] && [ -n "${REP_OWNER_DB_PASSWORD:-}" ]; then
  DDL_AS=(-U "$REP_OWNER_DB_USER")
  SAVED_PGPASSWORD="${PGPASSWORD:-}"
  export PGPASSWORD="$REP_OWNER_DB_PASSWORD"
fi
restore_pgpassword() { [ -n "${SAVED_PGPASSWORD:-}" ] && export PGPASSWORD="$SAVED_PGPASSWORD" || unset PGPASSWORD; }
ROWS="INSERT INTO reports_examples.regions(id, name) VALUES (99991, 'should-fail')"

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

# Подходит ли уже пароль из .env: если роль логинится, менять её не нужно — и не
# обязательно иметь на это права (ALTER ROLE доступен суперпользователю).
role_login() {
  PGPASSWORD="$current" psql -h "$PGHOST" -p "$PGPORT" -U "$RO_USER" -d "$PGDATABASE" -tAc "SELECT 1" >/dev/null 2>&1
}

exists="$(psql "${DDL_AS[@]}" -tAc "SELECT 1 FROM pg_roles WHERE rolname = '$RO_USER'")"
if [ -z "$exists" ]; then
  if psql "${DDL_AS[@]}" -v ON_ERROR_STOP=1 -c "CREATE ROLE $RO_USER LOGIN PASSWORD '$current'" 2>/dev/null; then
    echo "роль $RO_USER создана"
  else
    echo "ОШИБКА: роль $RO_USER не создана — нужны права суперпользователя (запустите под postgres)" >&2
    exit 1
  fi
elif role_login; then
  echo "роль $RO_USER уже с этим паролем — менять не нужно"
elif psql "${DDL_AS[@]}" -v ON_ERROR_STOP=1 -c "ALTER ROLE $RO_USER LOGIN PASSWORD '$current'" 2>/dev/null; then
  echo "роль $RO_USER обновлена"
else
  echo "ОШИБКА: пароль $RO_USER не подходит, а сменить его нет прав (нужен суперпользователь)" >&2
  exit 1
fi

psql "${DDL_AS[@]}" -v ON_ERROR_STOP=1 -f "$ROOT/db/roles.sql" >/dev/null
echo "права: SELECT на схему reports_examples, запись запрещена"

# Страховки (statement_timeout, read-only по умолчанию) требуют прав суперпользователя:
# под обычной ролью приложения пропускаем — сам бэкенд задаёт тайм-аут в DSN.
if ! psql "${DDL_AS[@]}" -v ON_ERROR_STOP=1 -f "$ROOT/db/roles-hardening.sql" >/dev/null 2>&1; then
  echo "   замечание: ALTER ROLE пропущен — нужны права суперпользователя (запустите под postgres)"
fi
restore_pgpassword

# --- проверки на живом соединении -----------------------------------------
set +e
ok_read="$(psql "host=${PGHOST} port=${PGPORT} user=${RO_USER} password=${current} dbname=${PGDATABASE}" \
  -tAc "SELECT count(*) FROM reports_examples.sales" 2>&1)"
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
# сообщение зависит от локали сервера: с русской локалью это «нет доступа к таблице …»
if echo "$write_out" | grep -qi "read-only\|permission denied\|только для чтения\|нет доступа"; then
  echo "запись запрещена: $(echo "$write_out" | head -1)"
else
  echo "ОШИБКА: роль смогла писать в базу: $write_out" >&2
  exit 1
fi
