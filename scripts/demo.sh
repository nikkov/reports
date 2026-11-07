#!/usr/bin/env bash
# Локальная демонстрация: поднимает бэкенд (:8080) и предпросмотр собранного фронтенда (:5173),
# ждёт готовности, печатает ссылки и работает до Ctrl+C.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
. "$ROOT/scripts/env.sh"

PORT="${REP_PORT:-8080}"
WEB_PORT="${REP_WEB_PORT:-5173}"
REPORT="${REP_REPORT:-sales-summary}"
PARAMS="${REP_PARAMS:-from=2026-01-01&to=2026-03-31}"

[ -x "$ROOT/backend/build/rep" ] || { echo "нет собранного бэкенда: ./scripts/build.sh"; exit 1; }
[ -d "$ROOT/frontend/dist" ] || { echo "нет собранного фронтенда: ./scripts/build.sh"; exit 1; }

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
echo "== бэкенд =="
( cd "$ROOT" && REP_PORT="$PORT" ./backend/build/rep > /tmp/rep-backend.log 2>&1 ) &
BACK_PID=$!

for i in $(seq 1 40); do
  if curl -sf -m 1 "http://127.0.0.1:$PORT/api/health" >/dev/null; then break; fi
  sleep 0.25
done
curl -sf -m 2 "http://127.0.0.1:$PORT/api/health" >/dev/null || {
  echo "бэкенд не поднялся, лог: /tmp/rep-backend.log"; tail -20 /tmp/rep-backend.log; exit 1;
}
echo "ok: $(curl -s "http://127.0.0.1:$PORT/api/health")"

echo "== фронтенд (preview собранного dist) =="
( cd "$ROOT/frontend" && REP_API="http://127.0.0.1:$PORT" npx vite preview --port "$WEB_PORT" --host 127.0.0.1 > /tmp/rep-web.log 2>&1 ) &
WEB_PID=$!

for i in $(seq 1 40); do
  if curl -sf -m 1 "http://127.0.0.1:$WEB_PORT/report.html" >/dev/null; then break; fi
  sleep 0.25
done

URL="http://127.0.0.1:$WEB_PORT/report.html?report=$REPORT&$PARAMS"
echo
echo "Список:  $URL"
echo "Спайк:   http://127.0.0.1:$WEB_PORT/ (песочница рендерера)"
echo "API:     http://127.0.0.1:$PORT/api/health
Почта:   ${REP_SMTP_HOST}:${REP_SMTP_PORT} (письма в /tmp/rep-mail, если приёмник локальный)"
echo "Логи:    /tmp/rep-backend.log, /tmp/rep-web.log"
echo "Ctrl+C — остановить."
echo

wait
