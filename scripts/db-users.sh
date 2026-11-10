#!/usr/bin/env bash
# Эксплуатационные роли PostgreSQL (ADR-75) — создание и обновление.
#
#   ./scripts/db-users.sh                     — создать/обновить reporting_srv и reporting_ro,
#                                               пароли взять из backend/.env (нет — сгенерировать)
#   ./scripts/db-users.sh --rotate            — новые пароли для обеих ролей (пишутся в .env)
#   ./scripts/db-users.sh --role reporting_ro — только одна роль
#   ./scripts/db-users.sh --password-stdin    — пароли из stdin: первая строка srv, вторая ro
#   ./scripts/db-users.sh --dry-run           — показать, что будет сделано
#
# Кто что может (права выдаёт db/roles.sql от имени владельца объектов):
#   reporting_srv — сам сервер: SELECT/INSERT/UPDATE/DELETE в схеме reports,
#                   чтение reports_examples, без DDL и без создания ролей;
#   reporting_ro  — только чтение данных отчётов (reports_examples).
#
# Скрипт идемпотентен: существующие роли не удаляются, пароль меняется только при
# --rotate/--password-stdin. Пароли не печатаются: при генерации они пишутся в
# backend/.env (chmod 600), а в лог попадает только факт.
#
# Нужны права создавать роли (суперпользователь или CREATEROLE): это шаг установки,
# в эксплуатации суперпользователь не используется.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
. "$ROOT/scripts/env.sh"

ADMIN_USER="${PGADMIN_USER:-$(whoami)}"
ADMIN_HOST="${PGADMIN_HOST:-/var/run/postgresql}"
PORT="${REP_LOCAL_PORT:-5432}"
DB="${REP_DB:-${PGDATABASE:-reporting}}"
ENV_FILE="$ROOT/backend/.env"

SRV_USER="${REP_SRV_DB_USER:-reporting_srv}"
RO_USER="${REP_RO_DB_USER:-reporting_ro}"

ROTATE=0
DRY=0
FROM_STDIN=0
ONLY=""
while [ $# -gt 0 ]; do
  case "$1" in
    --rotate) ROTATE=1 ;;
    --dry-run) DRY=1 ;;
    --password-stdin) FROM_STDIN=1 ;;
    --role) shift; ONLY="${1:-}" ;;
    --role=*) ONLY="${1#--role=}" ;;
    -h|--help) sed -n '2,26p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "неизвестный аргумент: $1" >&2; exit 2 ;;
  esac
  shift
done

[ -z "$ONLY" ] || [ "$ONLY" = "$SRV_USER" ] || [ "$ONLY" = "$RO_USER" ] || {
  echo "неизвестная роль: $ONLY (ожидается $SRV_USER или $RO_USER)" >&2; exit 2; }

die() { echo "ошибка: $*" >&2; exit 1; }
command -v psql >/dev/null || die "нужен psql (пакет postgresql-client)"

read_env() { [ -f "$ENV_FILE" ] && sed -n "s/^$1=//p" "$ENV_FILE" | head -1 || true; }
write_env() {  # write_env ИМЯ значение — обновить строку в .env или дописать
  local key="$1" value="$2"
  [ -f "$ENV_FILE" ] || : > "$ENV_FILE"
  if grep -q "^${key}=" "$ENV_FILE"; then
    sed -i "s|^${key}=.*|${key}=${value}|" "$ENV_FILE"
  else
    printf '%s=%s\n' "$key" "$value" >> "$ENV_FILE"
  fi
  chmod 600 "$ENV_FILE"
}

gen() { openssl rand -hex 16; }

# --- пароли ---------------------------------------------------------------------
SRV_PW="$(read_env PGPASSWORD)"
RO_PW="$(read_env REP_RO_DB_PASSWORD)"
CHANGED=0

if [ "$FROM_STDIN" = "1" ]; then
  IFS= read -r line1 || true
  IFS= read -r line2 || true
  case "$ONLY" in
    "$SRV_USER") SRV_PW="${line1:-}"; ;;
    "$RO_USER")  RO_PW="${line1:-}"; ;;
    *)           SRV_PW="${line1:-}"; RO_PW="${line2:-}"; ;;
  esac
  [ -n "$SRV_PW" ] || [ -n "$RO_PW" ] || die "stdin пуст: ожидались пароли по строкам"
  CHANGED=1
elif [ "$ROTATE" = "1" ]; then
  SRV_PW="$(gen)"; RO_PW="$(gen)"; CHANGED=1
else
  # создание: пароль из .env, иначе генерируем — роль без пароля не войдёт
  [ -n "$SRV_PW" ] || { SRV_PW="$(gen)"; CHANGED=1; }
  [ -n "$RO_PW" ]  || { RO_PW="$(gen)";  CHANGED=1; }
fi

WANT_SRV=1; WANT_RO=1
case "$ONLY" in
  "$SRV_USER") WANT_RO=0 ;;
  "$RO_USER")  WANT_SRV=0 ;;
esac

# --- проверка соединения --------------------------------------------------------
ADMIN=(psql -h "$ADMIN_HOST" -p "$PORT" -U "$ADMIN_USER" -d "$DB" -v ON_ERROR_STOP=1 -q)
if ! "${ADMIN[@]}" -tAc "SELECT 1" >/dev/null 2>&1; then
  # возможно, нужен пароль администратора или роль ещё не имеет доступа к базе
  die "не удалось подключиться как $ADMIN_USER к $ADMIN_HOST:$PORT/$DB (нужны права создавать роли)"
fi

if [ "$DRY" = "1" ]; then
  echo "будет сделано:"
  if [ "$WANT_SRV" = "1" ]; then echo "  роль $SRV_USER: LOGIN, NOSUPERUSER NOCREATEDB NOCREATEROLE + пароль"; fi
  if [ "$WANT_RO" = "1" ]; then echo "  роль $RO_USER: LOGIN, NOSUPERUSER NOCREATEDB NOCREATEROLE + пароль"; fi
  echo "  права — по db/roles.sql (srv: DML в reports + чтение reports_examples; ro: только чтение)"
  if [ "$CHANGED" = "1" ]; then echo "  пароли будут обновлены в backend/.env"; fi
  exit 0
fi

echo "== роли в $ADMIN_HOST:$PORT/$DB (администратор: $ADMIN_USER)"

create_role() {  # create_role ИМЯ ПАРОЛЬ
  local role="$1" pw="$2"
  "${ADMIN[@]}" -c "DO \$\$ BEGIN
      IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = '$role') THEN
        CREATE ROLE \"$role\" LOGIN;
      END IF;
    END \$\$;"
  # пароль и флаги ставим всегда: так .env остаётся единственным источником истины
  "${ADMIN[@]}" -c "ALTER ROLE \"$role\" LOGIN PASSWORD '$pw';"
  "${ADMIN[@]}" -c "ALTER ROLE \"$role\" NOSUPERUSER NOCREATEDB NOCREATEROLE NOREPLICATION;"
  echo "   роль $role: пароль и флаги выставлены"
}

if [ "$WANT_SRV" = "1" ]; then create_role "$SRV_USER" "$SRV_PW"; fi
if [ "$WANT_RO" = "1" ]; then create_role "$RO_USER" "$RO_PW"; fi

# --- права: db/roles.sql под владельцем объектов --------------------------------
# ALTER DEFAULT PRIVILEGES действует только для объектов этого владельца, поэтому
# права выдаёт именно он: подключаемся по TCP с его паролем из окружения.
if [ -n "${REP_OWNER_DB_USER:-}" ] && [ -n "${REP_OWNER_DB_PASSWORD:-}" ]; then
  GRANT_LOG="$ROOT/tmp/db-users-grant.log"
  mkdir -p "$ROOT/tmp"
  if PGPASSWORD="$REP_OWNER_DB_PASSWORD" psql -h 127.0.0.1 -p "$PORT" -U "$REP_OWNER_DB_USER" \
       -d "$DB" -v ON_ERROR_STOP=1 -q -f "$ROOT/db/roles.sql" >"$GRANT_LOG" 2>&1; then
    echo "   права выданы владельцем ($REP_OWNER_DB_USER) по db/roles.sql"
  else
    echo "ВНИМАНИЕ: db/roles.sql не применился: $(tail -1 "$GRANT_LOG")"
  fi
  rm -f "$GRANT_LOG"
else
  echo "ВНИМАНИЕ: REP_OWNER_DB_USER/PASSWORD не заданы — права выдайте от владельца объектов"
fi

# --- запись паролей в .env ------------------------------------------------------
if [ "$CHANGED" = "1" ]; then
  if [ "$WANT_SRV" = "1" ]; then write_env PGPASSWORD "$SRV_PW"; fi
  if [ "$WANT_RO" = "1" ]; then write_env REP_RO_DB_PASSWORD "$RO_PW"; fi
  echo "   пароли обновлены в backend/.env (chmod 600)"
fi

# --- проверка прав --------------------------------------------------------------
check() {  # check ИМЯ SQL ОЖИДАНИЕ
  local got
  got="$("${ADMIN[@]}" -tAc "$2" 2>/dev/null | tr -d '[:space:]')"
  if [ "$got" = "$3" ]; then
    printf '   %-40s ok\n' "$1"
  else
    printf '   %-40s ДЕФЕКТ (получено: %s, ожидалось: %s)\n' "$1" "${got:-—}" "$3"
    return 1
  fi
}

fail=0
if [ "$WANT_SRV" = "1" ]; then
  echo "== $SRV_USER"
  check "подключается" "SELECT count(*) FROM pg_roles WHERE rolname = '$SRV_USER'" "1" || fail=1
  check "SELECT в ядре" "SELECT has_table_privilege('$SRV_USER','reports.report_templates','SELECT')" "t" || fail=1
  check "запись в ядро (INSERT)" "SELECT has_table_privilege('$SRV_USER','reports.report_templates','INSERT')" "t" || fail=1
  check "чтение примеров" "SELECT has_table_privilege('$SRV_USER','reports_examples.sales','SELECT')" "t" || fail=1
  check "запись в примеры запрещена" "SELECT has_table_privilege('$SRV_USER','reports_examples.sales','INSERT')" "f" || fail=1
  check "CREATE в схеме запрещён" "SELECT has_schema_privilege('$SRV_USER','reports','CREATE')" "f" || fail=1
  check "не суперпользователь" "SELECT rolsuper FROM pg_roles WHERE rolname = '$SRV_USER'" "f" || fail=1
  if PGPASSWORD="$SRV_PW" psql -h 127.0.0.1 -p "$PORT" -U "$SRV_USER" -d "$DB" -tAc "SELECT 1" >/dev/null 2>&1; then
    printf '   %-40s ok\n' "вход по паролю"
  else
    printf '   %-40s ДЕФЕКТ\n' "вход по паролю"; fail=1
  fi
fi

if [ "$WANT_RO" = "1" ]; then
  echo "== $RO_USER"
  check "чтение примеров" "SELECT has_table_privilege('$RO_USER','reports_examples.sales','SELECT')" "t" || fail=1
  check "запись в примеры запрещена" "SELECT has_table_privilege('$RO_USER','reports_examples.sales','INSERT')" "f" || fail=1
  check "ядро закрыто" "SELECT has_table_privilege('$RO_USER','reports.report_templates','SELECT')" "f" || fail=1
  check "не суперпользователь" "SELECT rolsuper FROM pg_roles WHERE rolname = '$RO_USER'" "f" || fail=1
  if PGPASSWORD="$RO_PW" psql -h 127.0.0.1 -p "$PORT" -U "$RO_USER" -d "$DB" -tAc \
       "SELECT count(*) FROM reports_examples.sales" >/dev/null 2>&1; then
    printf '   %-40s ok\n' "вход по паролю и SELECT"
  else
    printf '   %-40s ДЕФЕКТ\n' "вход по паролю и SELECT"; fail=1
  fi
fi

[ "$fail" = "0" ] || die "проверки не прошли — смотрите строки ДЕФЕКТ выше"
echo "OK: роли готовы. Сервер работает под $SRV_USER, отчёты читаются под $RO_USER."
