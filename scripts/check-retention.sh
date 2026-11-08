#!/usr/bin/env bash
# Проверка ретеншена журналов (ADR-59): старые записи audit_log и subscription_runs
# удаляются, свежие остаются. Поднимает отдельный экземпляр бэкенда.
#   ./scripts/check-retention.sh
set -uo pipefail
cd "$(dirname "$0")/.."
source scripts/env.sh
set +e

PORT="${REP_RETENTION_PORT:-8095}"
DAYS=30
BASE="http://127.0.0.1:$PORT"
LOG="$(mktemp)"
PID=""
FAILS=0
MARK="retention_test_$$"

fail() { echo "  ✗ $*"; FAILS=$((FAILS + 1)); }
ok() { echo "  ✓ $*"; }

cleanup() {
  psql -q -c "delete from audit_log where actor like '${MARK}%'" >/dev/null 2>&1
  psql -q -c "delete from report_subscriptions where title like '${MARK}%'" >/dev/null 2>&1
}
trap cleanup EXIT

cleanup
# подписка-болванка: нужна как родитель для subscription_runs
SUB_ID=$(psql -qtAc "insert into report_subscriptions (report_id, title, cron, params, format, recipients, enabled)
                    values ('sales-summary', '${MARK} sub', '* * * * *', '{}'::jsonb, 'csv', array['test@example.test'], false)
                    returning id" | tail -1 | tr -d ' ')
[ -n "$SUB_ID" ] || { echo "не удалось создать подписку"; exit 1; }

# три старые записи и две свежие в каждом журнале
psql -q >/dev/null <<SQL
insert into audit_log (at, actor, action, object_type, object_id)
select now() - interval '40 days', '${MARK}_old', 'test.old', 'test', 'x' from generate_series(1, 3);
insert into audit_log (at, actor, action, object_type, object_id)
select now() - interval '5 days', '${MARK}_new', 'test.new', 'test', 'x' from generate_series(1, 2);
insert into subscription_runs (subscription_id, started_at, ok)
select ${SUB_ID}, now() - interval '40 days', true from generate_series(1, 3);
insert into subscription_runs (subscription_id, started_at, ok)
select ${SUB_ID}, now() - interval '5 days', true from generate_series(1, 2);
SQL

before_audit_old=$(psql -tAc "select count(*) from audit_log where actor = '${MARK}_old'")
before_audit_new=$(psql -tAc "select count(*) from audit_log where actor = '${MARK}_new'")
before_runs_old=$(psql -tAc "select count(*) from subscription_runs where subscription_id = ${SUB_ID} and started_at < now() - interval '30 days'")
before_runs_new=$(psql -tAc "select count(*) from subscription_runs where subscription_id = ${SUB_ID} and started_at >= now() - interval '30 days'")
echo "до чистки: audit старых $before_audit_old / свежих $before_audit_new, runs старых $before_runs_old / свежих $before_runs_new"

env REP_PORT="$PORT" REP_SCHEDULER=0 REP_LOG_RETENTION_DAYS="$DAYS" ./backend/build/rep >"$LOG" 2>&1 &
PID=$!
for _ in $(seq 1 60); do
  curl -sf "$BASE/api/health" >/dev/null 2>&1 && break
  sleep 0.25
done
# ждём записи о чистке в журнале (стартует сразу после запуска)
for _ in $(seq 1 60); do
  [ "$(psql -tAc "select count(*) from audit_log where action = 'logs.purge' and at > now() - interval '2 minutes'")" -gt 0 ] && break
  sleep 0.25
done

after_audit_old=$(psql -tAc "select count(*) from audit_log where actor = '${MARK}_old'")
after_audit_new=$(psql -tAc "select count(*) from audit_log where actor = '${MARK}_new'")
after_runs_old=$(psql -tAc "select count(*) from subscription_runs where subscription_id = ${SUB_ID} and started_at < now() - interval '30 days'")
after_runs_new=$(psql -tAc "select count(*) from subscription_runs where subscription_id = ${SUB_ID} and started_at >= now() - interval '30 days'")

[ "$after_audit_old" = "0" ] && ok "audit_log: старые записи удалены ($before_audit_old → 0)" || fail "audit_log: осталось старых $after_audit_old"
[ "$after_audit_new" = "$before_audit_new" ] && ok "audit_log: свежие записи сохранены ($after_audit_new)" || fail "audit_log: свежих $after_audit_new вместо $before_audit_new"
[ "$after_runs_old" = "0" ] && ok "subscription_runs: старые записи удалены ($before_runs_old → 0)" || fail "subscription_runs: осталось старых $after_runs_old"
[ "$after_runs_new" = "$before_runs_new" ] && ok "subscription_runs: свежие записи сохранены ($after_runs_new)" || fail "subscription_runs: свежих $after_runs_new вместо $before_runs_new"

PURGE=$(psql -tAc "select details::text from audit_log where action = 'logs.purge' order by at desc limit 1")
case "$PURGE" in
  *'"days"'*) ok "чистка записана в журнал: $PURGE" ;;
  *) fail "нет записи logs.purge в журнале" ;;
esac

HEALTH=$(curl -s "$BASE/api/health")
case "$HEALTH" in
  *'"logRetentionDays":30'*) ok "health отдаёт глубину хранения (30)" ;;
  *) fail "в /api/health нет logRetentionDays=30: $HEALTH" ;;
esac

kill "$PID" 2>/dev/null
for _ in $(seq 1 40); do kill -0 "$PID" 2>/dev/null || break; sleep 0.1; done
kill -9 "$PID" 2>/dev/null

# --- глубина 0: журналы не чистятся вовсе ---
echo "глубина 0 — хранение бессрочное"
psql -q -c "insert into audit_log (at, actor, action, object_type, object_id)
            values (now() - interval '400 days', '${MARK}_forever', 'test.forever', 'test', 'x')" >/dev/null
env REP_PORT="$PORT" REP_SCHEDULER=0 REP_LOG_RETENTION_DAYS=0 ./backend/build/rep >"$LOG" 2>&1 &
PID=$!
for _ in $(seq 1 60); do
  curl -sf "$BASE/api/health" >/dev/null 2>&1 && break
  sleep 0.25
done
sleep 1
KEPT=$(psql -tAc "select count(*) from audit_log where actor = '${MARK}_forever'")
[ "$KEPT" = "1" ] && ok "запись 400-дневной давности сохранена при глубине 0" || fail "при глубине 0 запись исчезла"
case "$(curl -s "$BASE/api/health")" in
  *'"logRetentionDays":0'*) ok "health отдаёт глубину хранения (0)" ;;
  *) fail "health не сообщает глубину 0" ;;
esac
kill "$PID" 2>/dev/null
for _ in $(seq 1 40); do kill -0 "$PID" 2>/dev/null || break; sleep 0.1; done
kill -9 "$PID" 2>/dev/null

echo
if [ "$FAILS" -eq 0 ]; then
  echo "RETENTION: все проверки пройдены"
  exit 0
fi
echo "RETENTION: провалов $FAILS"
exit 1
