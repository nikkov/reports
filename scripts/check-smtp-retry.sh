#!/usr/bin/env bash
# Проверка повторов рассылки (ADR-61): сбой SMTP не теряет письмо, попытки нумеруются,
# пауза растёт, после исчерпания попыток подписка остаётся без retry и пишет given_up.
# SMTP здесь заведомо недоступен — порт 1.
#   ./scripts/check-smtp-retry.sh
set -uo pipefail
cd "$(dirname "$0")/.."
source scripts/env.sh
set +e

PORT="${REP_RETRY_PORT:-8096}"
BASE="http://127.0.0.1:$PORT"
ATTEMPTS=3
LOG="$(mktemp)"
PID=""
FAILS=0
MARK="retry_test_$$"

ok() { echo "  ✓ $*"; }
fail() { echo "  ✗ $*"; FAILS=$((FAILS + 1)); }

cleanup() {
  psql -q -c "delete from report_subscriptions where title like '${MARK}%'" >/dev/null 2>&1
}
trap cleanup EXIT
cleanup

SUB_ID=$(psql -qtAc "insert into report_subscriptions
    (report_id, title, cron, params, format, recipients, enabled, next_run_at, timezone)
  values ('sales-aggregate', '${MARK}', '0 4 * * *',
          '{\"from\":\"2026-03-01\",\"to\":\"2026-03-10\"}'::jsonb, 'csv',
          array['retry@example.test'], true, now(), 'UTC')
  returning id" | tail -1 | tr -d ' ')
[ -n "$SUB_ID" ] || { echo "не удалось создать подписку"; exit 1; }
# Второй планировщик заберёт ту же подписку и сломает сценарий: проверка требует,
# чтобы на стенде работал только её экземпляр.
BUSY=""
for pid in $(pgrep -x rep); do
  env_dump=$(tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null)
  port=$(echo "$env_dump" | grep -o 'REP_PORT=[0-9]*' | cut -d= -f2)
  port=${port:-8080}
  echo "$env_dump" | grep -qx 'REP_SCHEDULER=0' && continue
  [ "$port" = "$PORT" ] && continue
  BUSY="$BUSY $pid(:$port)"
done
if [ -n "$BUSY" ]; then
  echo "на стенде уже работает планировщик:$BUSY"
  echo "остановите эти экземпляры или перезапустите их с REP_SCHEDULER=0 и повторите проверку"
  psql -qtAc "delete from report_subscriptions where id=$SUB_ID" >/dev/null
  exit 2
fi

echo "подписка $SUB_ID, SMTP заведомо недоступен (127.0.0.1:1), пауза 2 с, попыток $ATTEMPTS"

# планировщик тикает раз в 30 с: три попытки укладываются примерно в 1,5 минуты
env REP_PORT="$PORT" REP_SCHEDULER=1 REP_ADMIN=1 \
    REP_SMTP_HOST=127.0.0.1 REP_SMTP_PORT=1 REP_SMTP_TLS=0 REP_SMTP_FROM=reports@example.test \
    REP_SMTP_RETRY_ATTEMPTS="$ATTEMPTS" REP_SMTP_RETRY_DELAY_SECONDS=2 \
    ./backend/build/rep >"$LOG" 2>&1 &
PID=$!
for _ in $(seq 1 60); do
  curl -sf "$BASE/api/health" >/dev/null 2>&1 && break
  sleep 0.25
done

wait_attempts() { # $1 — сколько попыток ждём
  for _ in $(seq 1 90); do
    [ "$(psql -tAc "select count(*) from subscription_runs where subscription_id=$SUB_ID")" -ge "$1" ] && return 0
    sleep 2
  done
  return 1
}

if wait_attempts 1; then
  ok "письмо не ушло, сбой записан в журнал"
else
  fail "первая попытка не дошла до журнала"
fi

RETRY=$(psql -tAc "select coalesce(retry_count,0)||'|'||coalesce(round(extract(epoch from (retry_at-now())))::text,'нет')||'|'||coalesce(ok::text,'') from report_subscriptions s
                   left join lateral (select ok from subscription_runs where subscription_id=s.id order by started_at desc limit 1) r on true
                   where s.id=$SUB_ID")
RC=${RETRY%%|*}
REST=${RETRY#*|}
IN=${REST%%|*}
[ "$RC" = "1" ] && ok "после сбоя назначен повтор (retry_count=1)" || fail "retry_count=$RC вместо 1"
case "$IN" in
  нет|-*) fail "retry_at не назначен" ;;
  *) ok "повтор запланирован через ${IN} с (пауза 2 с)" ;;
esac

if wait_attempts 2; then
  A2=$(psql -tAc "select attempt from subscription_runs where subscription_id=$SUB_ID order by started_at desc limit 1")
  [ "$A2" = "2" ] && ok "вторая попытка пронумерована attempt=2" || fail "вторая попытка с attempt=$A2"
else
  fail "вторая попытка не состоялась"
fi

# третья попытка исчерпывает лимит: retry снимается, в журнале given_up
if wait_attempts 3; then
  ok "третья попытка состоялась"
fi
for _ in $(seq 1 60); do
  GAVE=$(psql -tAc "select count(*) from audit_log where action='subscription.given_up' and object_id='$SUB_ID'")
  [ "$GAVE" -ge 1 ] && break
  sleep 2
done
RETRY_AFTER=$(psql -tAc "select coalesce(retry_at::text,'NULL')||'|'||retry_count from report_subscriptions where id=$SUB_ID")
[ "${RETRY_AFTER%%|*}" = "NULL" ] && [ "${RETRY_AFTER##*|}" = "0" ] \
  && ok "после $ATTEMPTS попыток retry снят, счётчик обнулён" \
  || fail "состояние после исчерпания: $RETRY_AFTER"
[ "${GAVE:-0}" -ge 1 ] && ok "факт отказа записан в журнал (subscription.given_up)" || fail "нет записи subscription.given_up"

ATTEMPTS_IN_DB=$(psql -tAc "select string_agg(attempt::text, ',' order by started_at) from subscription_runs where subscription_id=$SUB_ID")
echo "  · попытки в журнале: $ATTEMPTS_IN_DB (все с ok=false)"
psql -qtAc "update report_subscriptions set enabled=false where id=$SUB_ID" >/dev/null

# первый экземпляр останавливаем, иначе второй не займёт порт
kill "$PID" 2>/dev/null
for _ in $(seq 1 40); do kill -0 "$PID" 2>/dev/null || break; sleep 0.1; done
kill -9 "$PID" 2>/dev/null
PID=""

# --- успешный повтор: SMTP поднимается между попытками ---
echo "сбой → SMTP появился → повтор доставляет письмо"
SINK_PORT=$(python3 - <<'PY'
import socket
s = socket.socket(); s.bind(('127.0.0.1', 0)); print(s.getsockname()[1]); s.close()
PY
)
MAIL_DIR="$(mktemp -d)"
SUB2=$(psql -qtAc "insert into report_subscriptions
    (report_id, title, cron, params, format, recipients, enabled, next_run_at, timezone)
  values ('sales-aggregate', '${MARK}_ok', '0 4 * * *',
          '{\"from\":\"2026-03-01\",\"to\":\"2026-03-10\"}'::jsonb, 'csv',
          array['retry@example.test'], true, now(), 'Europe/Moscow')
  returning id" | tail -1 | tr -d ' ')

env REP_PORT="$PORT" REP_SCHEDULER=1 REP_ADMIN=1 \
    REP_SMTP_HOST=127.0.0.1 REP_SMTP_PORT="$SINK_PORT" REP_SMTP_TLS=0 REP_SMTP_FROM=reports@example.test \
    REP_SMTP_RETRY_ATTEMPTS=3 REP_SMTP_RETRY_DELAY_SECONDS=2 \
    ./backend/build/rep >"$LOG" 2>&1 &
PID=$!
for _ in $(seq 1 60); do
  curl -sf "$BASE/api/health" >/dev/null 2>&1 && break
  sleep 0.25
done
for _ in $(seq 1 60); do
  [ "$(psql -tAc "select count(*) from subscription_runs where subscription_id=$SUB2")" -ge 1 ] && break
  sleep 2
done
RC2=$(psql -tAc "select retry_count from report_subscriptions where id=$SUB2")
[ "$RC2" = "1" ] && ok "первая попытка без SMTP — повтор назначен" || fail "retry_count=$RC2 после сбоя"
python3 scripts/smtp-sink.py --port "$SINK_PORT" --dir "$MAIL_DIR" >/dev/null 2>&1 &
SINK_PID=$!
# ждём, пока приёмник начнёт принимать: иначе «повтор» снова упадёт по вине теста
for _ in $(seq 1 40); do
  (exec 3<>"/dev/tcp/127.0.0.1/$SINK_PORT") 2>/dev/null && break
  sleep 0.2
done
kill -0 "$SINK_PID" 2>/dev/null || fail "SMTP-приёмник не запустился (порт $SINK_PORT)"
for _ in $(seq 1 60); do
  [ "$(psql -tAc "select count(*) from subscription_runs where subscription_id=$SUB2 and ok")" -ge 1 ] && break
  sleep 2
done
STATE=$(psql -tAc "select case when retry_at is null then 'NULL' else 'set' end||'|'||retry_count from report_subscriptions where id=$SUB2")
[ "${STATE%%|*}" = "NULL" ] && [ "${STATE##*|}" = "0" ] \
  && ok "успешный повтор снял retry (retry_at=NULL, count=0)" \
  || fail "после успеха состояние: $STATE"
MAILS=$(find "$MAIL_DIR" -type f | wc -l | tr -d ' ')
[ "$MAILS" -ge 1 ] && ok "письмо доставлено в SMTP-приёмник (файлов: $MAILS)" || fail "письмо не дошло до приёмника"
LAST_OK=$(psql -tAc "select ok from subscription_runs where subscription_id=$SUB2 order by started_at desc limit 1")
[ "$LAST_OK" = "t" ] && ok "последняя попытка успешна" || fail "последняя попытка: ok=$LAST_OK"

kill "$SINK_PID" 2>/dev/null
psql -qtAc "update report_subscriptions set enabled=false where id=$SUB2" >/dev/null
kill "$PID" 2>/dev/null
for _ in $(seq 1 40); do kill -0 "$PID" 2>/dev/null || break; sleep 0.1; done
kill -9 "$PID" 2>/dev/null
rm -rf "$MAIL_DIR"

echo
if [ "$FAILS" -eq 0 ]; then
  echo "SMTP-RETRY: все проверки пройдены"
  exit 0
fi
echo "SMTP-RETRY: провалов $FAILS"
echo "лог экземпляра: $LOG"
exit 1
