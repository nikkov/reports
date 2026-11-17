#!/usr/bin/env bash
# Проверка Kerberos-аутентификации REST на живом стенде (ADR-122).
# Требует: ./scripts/dev-kerberos.sh и запущенный сервер с отчётом crm-kerberos.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
# shellcheck disable=SC1091
. ./scripts/env.sh

BASE="${REP_BASE_URL:-http://127.0.0.1:8080/reports}"
LOGIN="${REP_ADMIN_LOGIN:-admin}"
PASSWORD="${REP_ADMIN_PASSWORD:-$(grep -m1 '^REP_ADMIN_PASSWORD=' backend/.env 2>/dev/null | cut -d= -f2- || true)}"
if [ -z "$PASSWORD" ]; then
  echo "не задан REP_ADMIN_PASSWORD (ни в окружении, ни в backend/.env)" >&2
  exit 2
fi

cookie=$(curl -s -m 10 -c - -X POST "$BASE/api/auth/login" -H 'Content-Type: application/json' \
  -d "{\"login\":\"$LOGIN\",\"password\":\"$PASSWORD\"}" | awk '$6 == "rep_session" {print $7}')

response=$(curl -s -m 30 -b "rep_session=$cookie" -X POST "$BASE/api/report/data" \
  -H 'Content-Type: application/json' -d '{"reportId":"crm-kerberos","dataset":"negotiated","params":{}}')

if echo "$response" | grep -q 'demo@REP.LOCAL'; then
  echo "Kerberos: отчёт вернул данные пользователя demo@REP.LOCAL"
else
  echo "Kerberos: отчёт не получил данные" >&2
  echo "$response" >&2
  exit 1
fi
