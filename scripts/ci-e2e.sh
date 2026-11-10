#!/usr/bin/env bash
# Стенд для e2e «как в CI»: отдельная БД и локальный приёмник писем.
# Порт один — 8080 (ADR-80): сервер отдаёт и API, и статику, отдельного preview нет.
# Предполагает, что бэкенд и фронтенд уже собраны (CI собирает их шагами раньше).
#
#   ./scripts/ci-e2e.sh            — поднять стенд и прогнать Playwright
#   REP_CI_DB=reporting_ci ./scripts/ci-e2e.sh
#
# Скрипт намеренно не трогает рабочую БД: прогон изолирован по базе.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
. "$ROOT/scripts/env.sh"

DB="${REP_CI_DB:-reporting_ci}"
PORT="${REP_CI_PORT:-8080}"
MAIL_PORT="${REP_CI_SMTP_PORT:-1025}"
MAIL_DIR="${REP_CI_MAIL_DIR:-$ROOT/tmp/ci-mail}"

[ "$DB" != "reporting" ] || { echo "REP_CI_DB не может совпадать с рабочей базой reporting"; exit 1; }
export REP_DB="$DB"
export PGDATABASE="$DB"
# примеры ссылаются на ${REP_RO_DB_USER}/REP_RO_DB_PASSWORD: на тестовом стенде
# роль чтения совпадает с основной (ADR-77)
export REP_RO_DB_USER="${REP_RO_DB_USER:-$PGUSER}"
export REP_RO_DB_PASSWORD="${REP_RO_DB_PASSWORD:-$PGPASSWORD}"
LOGDIR="$ROOT/tmp/ci-logs"
mkdir -p "$LOGDIR" "$MAIL_DIR"

[ -x "$ROOT/backend/build/rep" ] || { echo "нет backend/build/rep: соберите бэкенд"; exit 1; }
[ -d "$ROOT/frontend/dist" ] || { echo "нет frontend/dist: соберите фронтенд"; exit 1; }
# статику отдаёт сам сервер: кладём сборку рядом с бинарником (ADR-79)
rm -rf "$ROOT/backend/build/web"
cp -a "$ROOT/frontend/dist" "$ROOT/backend/build/web"

PIDS=()
cleanup() {
  for pid in "${PIDS[@]:-}"; do kill "$pid" 2>/dev/null || true; done
  wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

step() { echo; echo "=== $* ==="; }

step "БД $DB: схема, синтетика, миграции"
REP_SKIP_TEMPLATES=1 "$ROOT/db/init.sh"

step "SMTP-приёмник :$MAIL_PORT"
rm -rf "$MAIL_DIR"/*
python3 "$ROOT/scripts/smtp-sink.py" --port "$MAIL_PORT" --dir "$MAIL_DIR" > "$LOGDIR/smtp.log" 2>&1 &
PIDS+=($!)

step "Бэкенд :$PORT"
REP_PORT="$PORT" REP_ADMIN=1 REP_AUTH=1 \
REP_SMTP_HOST=127.0.0.1 REP_SMTP_PORT="$MAIL_PORT" REP_SMTP_TLS=0 REP_SMTP_FROM=reports@example.test \
  "$ROOT/backend/build/rep" > "$LOGDIR/backend.log" 2>&1 &
PIDS+=($!)

for i in $(seq 1 60); do
  curl -sf -m 1 "http://127.0.0.1:$PORT/api/health" >/dev/null && break
  sleep 0.5
done
curl -sf -m 2 "http://127.0.0.1:$PORT/api/health" >/dev/null || {
  echo "бэкенд не поднялся:"; tail -30 "$LOGDIR/backend.log"; exit 1;
}
echo "health: $(curl -s "http://127.0.0.1:$PORT/api/health")"

step "Пользователи и шаблоны"
REP_DB="$DB" "$ROOT/scripts/auth-init.sh" >/dev/null
set -a
# shellcheck disable=SC1091
. "$ROOT/backend/.env"
set +a
REP_API="http://127.0.0.1:$PORT" node "$ROOT/scripts/templates-push.mjs" "$ROOT"/backend/templates/*.json | tail -1

step "Статика через сервер"
curl -sf -m 2 "http://127.0.0.1:$PORT/" >/dev/null || {
  echo "сервер не отдаёт страницы — проверьте backend/build/web"; exit 1;
}

step "Playwright"
cd "$ROOT/frontend"
# REP_API нужен и global-setup: он логинится именно в тот бэкенд, чьи сессии проверяет стенд
REP_API="http://127.0.0.1:$PORT" REP_E2E_BASE="http://127.0.0.1:$PORT" npx playwright test "$@"

echo
echo "CI-E2E: все e2e прошли"
