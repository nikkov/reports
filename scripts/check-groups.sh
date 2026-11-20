#!/usr/bin/env bash
# Проверка групп адресатов (ADR-179): подписка без адресов, только с группой, отправляется
# на все адреса группы плюс отдельные адреса, без дублей.
#
#   ./scripts/check-groups.sh
#
# Поднимает SMTP-sink и отдельный экземпляр rep на свободном порту, чтобы не мешать стенду.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
# shellcheck disable=SC1091
. "$ROOT/scripts/env.sh"

MARK="grp_$$"
MAIL_DIR="$(mktemp -d /tmp/rep-groups-XXXXXX)"
PIDS=()
cleanup() {
  for pid in "${PIDS[@]:-}"; do kill "$pid" 2>/dev/null || true; done
  rm -rf "$MAIL_DIR"
}
trap cleanup EXIT

free_port() {
  python3 - <<'PY'
import socket
s = socket.socket(); s.bind(('127.0.0.1', 0)); print(s.getsockname()[1]); s.close()
PY
}

PORT=$(free_port)
SINK_PORT=$(free_port)
API="http://127.0.0.1:$PORT"

python3 scripts/smtp-sink.py --port "$SINK_PORT" --dir "$MAIL_DIR" >/dev/null 2>&1 &
PIDS+=($!)
for _ in $(seq 1 40); do (exec 3<>"/dev/tcp/127.0.0.1/$SINK_PORT") 2>/dev/null && break; sleep 0.2; done

env REP_PORT="$PORT" REP_SCHEDULER=0 REP_AUTH=1 REP_ADMIN=1 \
    REP_SMTP_HOST=127.0.0.1 REP_SMTP_PORT="$SINK_PORT" REP_SMTP_TLS=0 \
    REP_SMTP_FROM=reports@example.test \
    ./backend/build/rep >/tmp/rep-groups.log 2>&1 &
PIDS+=($!)
for _ in $(seq 1 60); do curl -sf "$API/api/health" >/dev/null 2>&1 && break; sleep 0.25; done
curl -sf "$API/api/health" >/dev/null || { echo "сервер не поднялся"; exit 1; }

ADMIN_PASSWORD="$(grep '^REP_ADMIN_PASSWORD=' backend/.env | cut -d= -f2-)"
JAR="$(mktemp)"
curl -sf -c "$JAR" -X POST "$API/api/auth/login" -H 'Content-Type: application/json' \
  -d "{\"login\":\"admin\",\"password\":\"$ADMIN_PASSWORD\"}" >/dev/null || { echo "логин не прошёл"; exit 1; }

# группа из двух адресов; один из них продублируем отдельным адресом — письмо должно быть одно
GROUP=$(curl -sf -b "$JAR" -X POST "$API/api/admin/contact-groups" -H 'Content-Type: application/json' \
  -d "{\"key\":\"$MARK\",\"title\":\"Группа $MARK\",\"emails\":[\"g1@example.test\",\"g2@example.test\"]}")
GROUP_ID=$(printf '%s' "$GROUP" | python3 -c 'import json,sys; print(json.load(sys.stdin)["id"])')

SUB=$(curl -sf -b "$JAR" -X POST "$API/api/admin/subscriptions" -H 'Content-Type: application/json' \
  -d "{\"reportId\":\"sales-summary\",\"title\":\"$MARK\",\"cron\":\"0 9 * * 1-5\",\"timezone\":\"UTC\",\"recipients\":[\"manual@example.test\",\"g2@example.test\"],\"recipientGroups\":[\"$MARK\"],\"format\":\"csv\",\"params\":{\"from\":\"2026-01-01\",\"to\":\"2026-03-31\"}}")
SUB_ID=$(printf '%s' "$SUB" | python3 -c 'import json,sys; print(json.load(sys.stdin)["id"])')

curl -sf -b "$JAR" -X POST "$API/api/admin/subscriptions/$SUB_ID/run" >/dev/null
for _ in $(seq 1 40); do [ "$(ls -1 "$MAIL_DIR" | wc -l)" -ge 3 ] && break; sleep 0.5; done
sleep 1

SENT=$(ls -1 "$MAIL_DIR" | wc -l)
TO=$(grep -h '^To:' "$MAIL_DIR"/* 2>/dev/null | sed 's/^To: //' | tr -d '\r' | sort)
echo "писем: $SENT"
echo "$TO" | sed 's/^/  /'

FAIL=0
[ "$SENT" -eq 3 ] || { echo "ожидалось 3 письма (g1, g2, manual), получено $SENT"; FAIL=1; }
echo "$TO" | grep -q 'g1@example.test' || { echo "нет письма адресату из группы g1@"; FAIL=1; }
echo "$TO" | grep -q 'manual@example.test' || { echo "нет письма на отдельный адрес"; FAIL=1; }
[ "$(echo "$TO" | grep -c 'g2@example.test')" -eq 1 ] || { echo "g2@ должен получить одно письмо, а не два"; FAIL=1; }

# занятую группу удалить нельзя
CODE=$(curl -s -o /dev/null -w '%{http_code}' -b "$JAR" -X DELETE "$API/api/admin/contact-groups/$GROUP_ID")
[ "$CODE" = "409" ] || { echo "удаление занятой группы: ожидался 409, получен $CODE"; FAIL=1; }

# после снятия подписки группа удаляется
curl -sf -b "$JAR" -X DELETE "$API/api/admin/subscriptions/$SUB_ID" >/dev/null
CODE=$(curl -s -o /dev/null -w '%{http_code}' -b "$JAR" -X DELETE "$API/api/admin/contact-groups/$GROUP_ID")
[ "$CODE" = "200" ] || { echo "удаление свободной группы: ожидался 200, получен $CODE"; FAIL=1; }

if [ "$FAIL" = "0" ]; then
  echo "группы адресатов: OK"
else
  echo "группы адресатов: FAIL"
  exit 1
fi
