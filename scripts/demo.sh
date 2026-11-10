#!/usr/bin/env bash
# Локальная демонстрация: один процесс — и API, и собранный фронтенд (ADR-79).
# Ни nginx, ни node для раздачи статики не нужны.
#
#   ./scripts/demo.sh                   — бэкенд :8080 отдаёт /api и страницы из backend/build/web
#   REP_PORT=8081 ./scripts/demo.sh     — другой порт
#   REP_WEB_PORT=5173 ./scripts/demo.sh — прежний режим: отдельный vite preview (для сверки)
#
# Статика берётся из backend/build/web (её кладёт ./scripts/build.sh); если каталога нет,
# но есть frontend/dist — он копируется на место. Совсем без сборки сервер тоже поднимется:
# он отдаст только API и предупредит в логе.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
. "$ROOT/scripts/env.sh"

PORT="${REP_PORT:-8080}"
WEB_PORT="${REP_WEB_PORT:-}"
REPORT="${REP_REPORT:-sales-summary}"
PARAMS="${REP_PARAMS:-from=2026-01-01&to=2026-03-31}"

[ -x "$ROOT/backend/build/rep" ] || { echo "нет собранного бэкенда: ./scripts/build.sh"; exit 1; }

# Комплект: статика рядом с бинарником. Сборка dist уже есть — просто копируем.
if [ ! -d "$ROOT/backend/build/web" ]; then
  if [ -d "$ROOT/frontend/dist" ]; then
    cp -a "$ROOT/frontend/dist" "$ROOT/backend/build/web"
    echo "статика: frontend/dist → backend/build/web"
  else
    echo "ВНИМАНИЕ: нет ни backend/build/web, ни frontend/dist — сервер отдаст только API"
  fi
fi

cleanup() {
  echo
  echo "останавливаю…"
  [ -n "${MAIL_PID:-}" ] && kill "$MAIL_PID" 2>/dev/null || true
  [ -n "${BACK_PID:-}" ] && kill "$BACK_PID" 2>/dev/null || true
  [ -n "${WEB_PID:-}" ] && kill "$WEB_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

if ss -ltn 2>/dev/null | grep -q ":$PORT "; then
  echo "порт $PORT занят — укажите другой: REP_PORT=8081 ./scripts/demo.sh"
  exit 1
fi

# Админка отчётов (ADR-26) включена на локальном стенде; наружу не выставлять.
export REP_ADMIN="${REP_ADMIN:-1}"
# Вход и роли (ADR-34). Пользователи создаются scripts/auth-init.sh.
export REP_AUTH="${REP_AUTH:-1}"

# Рассылка (ADR-46): на стенде — локальный приёмник писем, письма в /tmp/rep-mail.
# Боевые настройки — REP_SMTP_HOST/REP_SMTP_PORT/REP_SMTP_USER/REP_SMTP_PASSWORD/REP_SMTP_TLS.
if [ "${REP_SMTP_HOST:-}" = "" ]; then
  MAIL_PORT="${REP_SMTP_PORT:-1025}"
  if ! ss -ltn 2>/dev/null | grep -q ":$MAIL_PORT "; then
    rm -rf /tmp/rep-mail
    ( python3 "$ROOT/scripts/smtp-sink.py" --port "$MAIL_PORT" --dir /tmp/rep-mail > /tmp/rep-mail.log 2>&1 ) &
    MAIL_PID=$!
  fi
  export REP_SMTP_HOST="127.0.0.1"
  export REP_SMTP_PORT="$MAIL_PORT"
  export REP_SMTP_TLS="0"
  export REP_SMTP_FROM="reports@example.test"
fi

echo "== сервер (API + статика) =="
( cd "$ROOT" && REP_PORT="$PORT" ./backend/build/rep > /tmp/rep-backend.log 2>&1 ) &
BACK_PID=$!

for i in $(seq 1 40); do
  if curl -sf -m 1 "http://127.0.0.1:$PORT/api/health" >/dev/null; then break; fi
  sleep 0.25
done
curl -sf -m 2 "http://127.0.0.1:$PORT/api/health" >/dev/null || {
  echo "бэкенд не поднялся, лог: /tmp/rep-backend.log"; tail -20 /tmp/rep-backend.log; exit 1
}
echo "ok: $(curl -s "http://127.0.0.1:$PORT/api/health")"
grep -m1 "статика" /tmp/rep-backend.log || true

# Прежний режим для сверки: статику отдаёт отдельный vite preview.
WEB_BASE="http://127.0.0.1:$PORT"
if [ -n "$WEB_PORT" ]; then
  [ -d "$ROOT/frontend/dist" ] || { echo "нет frontend/dist для REP_WEB_PORT"; exit 1; }
  echo "== фронтенд (preview, отдельный процесс) =="
  ( cd "$ROOT/frontend" && REP_API="http://127.0.0.1:$PORT" npx vite preview --port "$WEB_PORT" --host 127.0.0.1 > /tmp/rep-web.log 2>&1 ) &
  WEB_PID=$!
  for i in $(seq 1 40); do
    if curl -sf -m 1 "http://127.0.0.1:$WEB_PORT/report.html" >/dev/null; then break; fi
    sleep 0.25
  done
  WEB_BASE="http://127.0.0.1:$WEB_PORT"
fi

echo
echo "Отчёт:   $WEB_BASE/report.html?report=$REPORT&$PARAMS"
echo "Список:  $WEB_BASE/"
echo "API:     http://127.0.0.1:$PORT/api/health"
echo "Почта:   ${REP_SMTP_HOST}:${REP_SMTP_PORT} (письма в /tmp/rep-mail, если приёмник локальный)"
echo "Логи:    /tmp/rep-backend.log${WEB_PORT:+, /tmp/rep-web.log}"
echo "Ctrl+C — остановить."
echo

wait
