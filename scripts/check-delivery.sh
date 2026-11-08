#!/usr/bin/env bash
# Устойчивость рассылки (ADR-62): «заём» на время отправки настраивается,
# обрыв процесса не теряет попытку, прерванная строка закрывается при старте,
# второй экземпляр не берёт занятую подписку.
#   ./scripts/check-delivery.sh
set -uo pipefail
set +m  # без уведомлений «Убито» от job control: процессы глушим сами
cd "$(dirname "$0")/.."
source scripts/env.sh
set +e

PORT1="${REP_DELIVERY_PORT1:-8097}"
PORT2="${REP_DELIVERY_PORT2:-8098}"
MARK="delivery_test_$$"
LOG1="$(mktemp)"
LOG2="$(mktemp)"
PIDS=()
SINK_PID=""
MAIL_DIR="$(mktemp -d)"
FAILS=0

ok() { echo "  ✓ $*"; }
fail() { echo "  ✗ $*"; FAILS=$((FAILS + 1)); }

cleanup() {
  for pid in "${PIDS[@]:-}"; do [ -n "$pid" ] && kill -9 "$pid" 2>/dev/null; done
  [ -n "$SINK_PID" ] && kill "$SINK_PID" 2>/dev/null
  psql -q -c "delete from report_subscriptions where title like '${MARK}%'" >/dev/null 2>&1
  psql -q -c "delete from report_templates where id = '${MARK}'" >/dev/null 2>&1
  rm -rf "$MAIL_DIR"
}
trap cleanup EXIT
cleanup

# --- медленный отчёт: отправка занимает ~6 с, этого хватает, чтобы убить процесс в середине
psql -q >/dev/null <<SQL
insert into report_templates (id, title, body) values ('${MARK}', 'Медленный отчёт',
 jsonb_build_object(
   'version', 2,
   'id', '${MARK}',
   'title', 'Медленный отчёт',
   'datasets', jsonb_build_object('s', jsonb_build_object('sql', 'SELECT 1 AS n FROM pg_sleep(6)')),
   'blocks', jsonb_build_array(jsonb_build_object('type','table','dataKey','s',
              'columns', jsonb_build_array(jsonb_build_object('field','n','title','N'))))
 ));
SQL
SUB_ID=$(psql -qtAc "insert into report_subscriptions
    (report_id, title, cron, params, format, recipients, enabled, next_run_at, timezone)
  values ('${MARK}', '${MARK}', '0 4 * * *', '{}'::jsonb, 'csv', array['delivery@example.test'], true, now(), 'UTC')
  returning id" | tail -1 | tr -d ' ')
[ -n "$SUB_ID" ] || { echo "не удалось создать подписку"; exit 1; }

SINK_PORT=$(python3 - <<'PY'
import socket
s = socket.socket(); s.bind(('127.0.0.1', 0)); print(s.getsockname()[1]); s.close()
PY
)
python3 scripts/smtp-sink.py --port "$SINK_PORT" --dir "$MAIL_DIR" >/dev/null 2>&1 &
SINK_PID=$!
for _ in $(seq 1 40); do (exec 3<>"/dev/tcp/127.0.0.1/$SINK_PORT") 2>/dev/null && break; sleep 0.2; done

start() { # $1 порт, $2 заём, $3 лог
  env REP_PORT="$1" REP_SCHEDULER=1 REP_SCHEDULER_TICK_SECONDS=2 REP_SUBSCRIPTION_LEASE_SECONDS="$2" \
      REP_SMTP_HOST=127.0.0.1 REP_SMTP_PORT="$SINK_PORT" REP_SMTP_TLS=0 REP_SMTP_FROM=reports@example.test \
      ./backend/build/rep >"$3" 2>&1 &
  local pid=$!
  PIDS+=("$pid")
  for _ in $(seq 1 60); do curl -sf "http://127.0.0.1:$1/api/health" >/dev/null 2>&1 && return 0; sleep 0.25; done
  return 1
}

# Чужой планировщик заберёт подписку и сломает сценарий — проверка требует
# работать без соседей (то же правило, что в check-smtp-retry.sh).
BUSY=""
for pid in $(pgrep -x rep); do
  env_dump=$(tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null)
  port=$(echo "$env_dump" | grep -o 'REP_PORT=[0-9]*' | cut -d= -f2)
  port=${port:-8080}
  echo "$env_dump" | grep -qx 'REP_SCHEDULER=0' && continue
  [ "$port" = "$PORT1" ] || [ "$port" = "$PORT2" ] && continue
  BUSY="$BUSY $pid(:$port)"
done
if [ -n "$BUSY" ]; then
  echo "на стенде уже работает планировщик:$BUSY"
  echo "остановите эти экземпляры или перезапустите их с REP_SCHEDULER=0 и повторите проверку"
  exit 2
fi

echo "сценарий 1: обрыв процесса во время отправки"
start "$PORT1" 10 "$LOG1" || { fail "экземпляр не поднялся"; exit 1; }

# ждём, пока подписка будет занята (заём) и появится незавершённая попытка
CLAIMED=0
for _ in $(seq 1 60); do
  INFLIGHT=$(psql -tAc "select count(*) from subscription_runs where subscription_id=$SUB_ID and finished_at is null")
  [ "$INFLIGHT" -ge 1 ] && { CLAIMED=1; break; }
  sleep 0.5
done
if [ "$CLAIMED" = "1" ]; then
  ok "попытка записана в журнал до отправки (finished_at = NULL)"
else
  fail "незавершённой попытки не появилось"
fi

LEASE=$(psql -tAc "select round(extract(epoch from (next_run_at - now()))) from report_subscriptions where id=$SUB_ID")
[ -n "$LEASE" ] && [ "$LEASE" -ge 5 ] && [ "$LEASE" -le 12 ] \
  && ok "заём выставлен из конфигурации: next_run_at через ${LEASE} с (REP_SUBSCRIPTION_LEASE_SECONDS=10)" \
  || fail "ожидался заём ~10 с, получено «${LEASE}»"

# второй экземпляр не должен взять занятую подписку
BEFORE=$(psql -tAc "select count(*) from subscription_runs where subscription_id=$SUB_ID")
start "$PORT2" 10 "$LOG2" || fail "второй экземпляр не поднялся"
sleep 5
AFTER=$(psql -tAc "select count(*) from subscription_runs where subscription_id=$SUB_ID")
[ "$BEFORE" = "$AFTER" ] && ok "второй экземпляр не взял занятую подписку (попыток $AFTER)" \
  || fail "второй экземпляр добавил попытку: было $BEFORE, стало $AFTER"

# убиваем первый экземпляр посреди отправки
kill -9 "${PIDS[0]}" 2>/dev/null
PIDS[0]=""
sleep 1
STILL_OPEN=$(psql -tAc "select count(*) from subscription_runs where subscription_id=$SUB_ID and finished_at is null")
[ "$STILL_OPEN" -ge 1 ] && ok "после kill -9 попытка осталась незавершённой — след не потерян" \
  || fail "незавершённых попыток не осталось (след потерян)"

kill -9 "${PIDS[1]}" 2>/dev/null
PIDS[1]=""
sleep 1

echo "сценарий 2: рестарт закрывает прерванную попытку и отправляет снова"
sleep 6 # чтобы прерванная попытка стала старше заёма (5 с)
start "$PORT1" 5 "$LOG1" || { fail "экземпляр после рестарта не поднялся"; exit 1; }

CLOSED=""
for _ in $(seq 1 40); do
  CLOSED=$(psql -tAc "select coalesce(error,'') from subscription_runs
                      where subscription_id=$SUB_ID and error like 'прервано%' limit 1")
  [ -n "$CLOSED" ] && break
  sleep 0.5
done
[ -n "$CLOSED" ] && ok "прерванная попытка закрыта при старте: «$CLOSED»" \
  || fail "прерванная попытка не закрыта"

DELIVERED=""
for _ in $(seq 1 90); do
  DELIVERED=$(psql -tAc "select count(*) from subscription_runs where subscription_id=$SUB_ID and ok")
  [ "$DELIVERED" -ge 1 ] && break
  sleep 0.5
done
[ "$DELIVERED" -ge 1 ] && ok "повторная отправка после обрыва прошла успешно" \
  || fail "повторной успешной отправки не было"

MAILS=$(find "$MAIL_DIR" -type f | wc -l | tr -d ' ')
[ "$MAILS" -ge 1 ] && ok "письмо доставлено в приёмник (файлов: $MAILS)" || fail "письмо не дошло"

NEXT=$(psql -tAc "select to_char(next_run_at AT TIME ZONE 'UTC','YYYY-MM-DD HH24:MI') from report_subscriptions where id=$SUB_ID")
DAY=$(psql -tAc "select to_char(next_run_at AT TIME ZONE 'UTC','DD') >= to_char(now() AT TIME ZONE 'UTC','DD') from report_subscriptions where id=$SUB_ID")
[ "$DAY" = "t" ] && ok "расписание после успеха пересчитано по cron ($NEXT UTC)" || fail "next_run_at не пересчитан: $NEXT"

echo
if [ "$FAILS" -eq 0 ]; then
  echo "DELIVERY: все проверки пройдены"
  exit 0
fi
echo "DELIVERY: провалов $FAILS"
echo "логи: $LOG1 $LOG2"
exit 1
