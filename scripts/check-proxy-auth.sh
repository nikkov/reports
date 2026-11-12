#!/usr/bin/env bash
# Проверка режима аутентификации на прокси (ADR-54): поднимает отдельный экземпляр
# бэкенда с REP_AUTH=proxy и проверяет заголовок, доверенные адреса, роли и автосоздание.
#   ./scripts/check-proxy-auth.sh
set -uo pipefail
cd "$(dirname "$0")/.."
source scripts/env.sh
# env.sh включает set -e; здесь нужны собственные проверки кодов ответов,
# а неудачный kill (процесс уже завершился) не должен обрывать прогон.
set +e

PORT="${REP_PROXY_PORT:-8092}"
BASE="http://127.0.0.1:$PORT"
LOG="$(mktemp)"
FAILS=0
PID=""

port_busy() { (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null; }

start() { # $1 — дополнительные присваивания вида "VAR=1 VAR2=x"
  if port_busy; then
    echo "порт $PORT занят — остановите прежний экземпляр"
    exit 1
  fi
  env REP_PORT="$PORT" REP_ADMIN=1 REP_AUTH=proxy $1 ./backend/build/rep >"$LOG" 2>&1 &
  PID=$!
  for _ in $(seq 1 60); do
    curl -sf "$BASE/api/health" >/dev/null 2>&1 && return 0
    sleep 0.25
  done
  echo "бэкенд не поднялся:"
  cat "$LOG"
  return 1
}

stop() {
  if [ -n "$PID" ]; then
    kill "$PID" 2>/dev/null || true
    for _ in $(seq 1 40); do kill -0 "$PID" 2>/dev/null || break; sleep 0.1; done
    kill -9 "$PID" 2>/dev/null || true
  fi
  PID=""
  sleep 0.3
}

code() { curl -s -o /tmp/rep-proxy.out -w '%{http_code}' "$@" || echo 000; }

check() { # $1 описание, $2 ожидаемый код, дальше — аргументы curl (адрес последним)
  local what="$1" want="$2"
  shift 2
  local got
  got=$(code "$@")
  if [ "$got" = "$want" ]; then
    printf '  ✓ %-46s %s\n' "$what" "$got"
  else
    printf '  ✗ %-46s ожидался %s, получен %s: %s\n' "$what" "$want" "$got" "$(head -c 120 /tmp/rep-proxy.out)"
    FAILS=$((FAILS + 1))
  fi
}

echo "режим прокси, доверенный адрес 127.0.0.1, автосоздание выключено"
start "REP_AUTH_PROXY_TRUSTED=127.0.0.1,::1"
check "без заголовка — отказ" 401 "$BASE/api/reports"
check "неизвестный пользователь — нет доступа" 403 -H "X-Remote-User: ghost" "$BASE/api/reports"
check "admin — каталог" 200 -H "X-Remote-User: admin" "$BASE/api/reports"
check "admin — админский API" 200 -H "X-Remote-User: admin" "$BASE/api/admin/audit"
check "viewer — каталог" 200 -H "X-Remote-User: viewer" "$BASE/api/reports"
check "viewer — админский API запрещён" 403 -H "X-Remote-User: viewer" "$BASE/api/admin/audit"
check "viewer — публикация запрещена" 403 -H "X-Remote-User: viewer" -X POST "$BASE/api/admin/reports/sales-summary/publish"
check "логин с пробелом — отказ" 401 -H "X-Remote-User: ivan petrov" "$BASE/api/reports"
check "логин с ; — отказ" 401 -H "X-Remote-User: ivan;drop" "$BASE/api/reports"
check "вход по паролю недоступен" 400 -H "X-Remote-User: admin" -X POST -H "Content-Type: application/json" -d '{}' "$BASE/api/auth/login"
check "выход отклоняется" 400 -H "X-Remote-User: admin" -X POST "$BASE/api/auth/logout"
ME=$(curl -s -H 'X-Remote-User: admin' "$BASE/api/auth/me")
case "$ME" in
  *'"mode":"proxy"'*'"role":"admin"'*) printf '  ✓ %-46s %s\n' "auth/me отдаёт личность прокси" "ok" ;;
  *) printf '  ✗ %-46s %s\n' "auth/me отдаёт личность прокси" "$ME"; FAILS=$((FAILS + 1)) ;;
esac
stop

echo "доверенный адрес не совпадает (10.0.0.5)"
start "REP_AUTH_PROXY_TRUSTED=10.0.0.5"
check "заголовок от чужого адреса не принимается" 403 -H "X-Remote-User: admin" "$BASE/api/reports"
stop

echo "автосоздание пользователей прокси"
LOGIN="proxy_test_$$"
start "REP_AUTH_PROXY_AUTOCREATE=1 REP_AUTH_PROXY_ROLE=viewer"
check "новый пользователь — каталог" 200 -H "X-Remote-User: $LOGIN" "$BASE/api/reports"
check "новый пользователь — только viewer" 403 -H "X-Remote-User: $LOGIN" "$BASE/api/admin/audit"
ROLE=$(psql -tAc "select role from users where login='$LOGIN'")
PWD_EMPTY=$(psql -tAc "select password_hash = '' from users where login='$LOGIN'")
if [ "${PWD_EMPTY:-f}" = "t" ]; then PWD_NOTE="пустой (вход по паролю невозможен)"; else PWD_NOTE="задан"; fi
printf '  · в справочнике: %s, роль %s, пароль %s\n' "$LOGIN" "${ROLE:-нет}" "$PWD_NOTE"
AUDIT=$(curl -s -H 'X-Remote-User: admin' "$BASE/api/admin/audit?action=user.autocreate&limit=5")
case "$AUDIT" in
  *"$LOGIN"*) printf '  ✓ %-46s %s\n' "автосоздание попало в журнал" "ok" ;;
  *) printf '  ✗ %-46s %s\n' "автосоздание попало в журнал" "$AUDIT"; FAILS=$((FAILS + 1)) ;;
esac
psql -qtAc "delete from users where login='$LOGIN'" >/dev/null
stop

echo "доменные имена: CORP\\ivanov, регистр не важен (ADR-93)"
DOM="CORP\\dom_proxy_$$"
DOM_OTHER="corp\\DOM_PROXY_$$"
start "REP_AUTH_PROXY_AUTOCREATE=1 REP_AUTH_PROXY_ROLE=viewer"
check "доменный логин — каталог" 200 -H "X-Remote-User: $DOM" "$BASE/api/reports"
check "тот же логин в другом регистре" 200 -H "X-Remote-User: $DOM_OTHER" "$BASE/api/reports"
COUNT=$(psql -tAc "select count(*) from users where lower(login) = lower('$DOM')" 2>/dev/null)
if [ "${COUNT:-0}" = "1" ]; then
  printf '  ✓ %-46s %s\n' "учётная запись ровно одна" "1"
else
  printf '  ✗ %-46s %s\n' "учётная запись ровно одна" "${COUNT:-нет}"
  FAILS=$((FAILS + 1))
fi
DOM_PWD=$(psql -tAc "select password_hash = '' from users where lower(login) = lower('$DOM')" 2>/dev/null)
if [ "${DOM_PWD:-f}" = "t" ]; then
  printf '  ✓ %-46s %s\n' "пароль пустой (вход только через прокси)" "ok"
else
  printf '  ✗ %-46s %s\n' "пароль пустой (вход только через прокси)" "${DOM_PWD:-нет}"
  FAILS=$((FAILS + 1))
fi
psql -qtAc "delete from users where lower(login) = lower('$DOM')" >/dev/null
stop

echo
if [ "$FAILS" -eq 0 ]; then
  echo "PROXY-AUTH: все проверки пройдены"
  exit 0
fi
echo "PROXY-AUTH: провалов $FAILS"
exit 1
