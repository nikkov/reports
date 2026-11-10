#!/usr/bin/env bash
# Загрузка шаблонов примеров в БД напрямую (ADR-21): без запущенного сервера и без API.
#
#   ./db/load-examples.sh                 — залить все шаблоны из backend/templates/*.json
#   ./db/load-examples.sh sales-summary   — только указанные (по id)
#   ./db/load-examples.sh --list          — что есть в файлах и что уже лежит в БД
#   ./db/load-examples.sh --dry-run       — показать план, ничего не менять
#   ./db/load-examples.sh --no-validate   — не проверять шаблоны (только для отладки)
#
# Чем отличается от scripts/templates-push.mjs: тому нужен поднятый сервер и админский
# API, этот скрипт пишет прямо в reports.report_templates. Нужен при первичной установке
# (сервер ещё не поднят) и для наката примеров на готовую базу.
#
# Поведение:
#   * шаблон проверяется валидатором (frontend/scripts/validate.mjs), если есть node;
#   * прежнее тело уходит в reports.report_templates_history — как при публикации;
#   * черновик сбрасывается: загруженная версия считается опубликованной;
#   * в reports.audit_log пишется template.load (actor = script);
#   * всё одной транзакцией: битый шаблон откатывает весь прогон.
#
# Пишет под ролью сервера (PGUSER = reporting_srv): ей разрешён DML в схеме reports.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
. "$ROOT/scripts/env.sh"

DB="${REP_DB:-${PGDATABASE:-reporting}}"
DIR="$ROOT/backend/templates"
VALIDATE=1
DRY=0
LIST=0
WANT=()

while [ $# -gt 0 ]; do
  case "$1" in
    --dry-run) DRY=1 ;;
    --list) LIST=1 ;;
    --no-validate) VALIDATE=0 ;;
    -h|--help) sed -n '2,26p' "${BASH_SOURCE[0]}"; exit 0 ;;
    -*) echo "неизвестный аргумент: $1" >&2; exit 2 ;;
    *) WANT+=("$1") ;;
  esac
  shift
done

die() { echo "ошибка: $*" >&2; exit 1; }
command -v psql >/dev/null || die "нужен psql (пакет postgresql-client)"
command -v python3 >/dev/null || die "нужен python3 (разбор id и title из JSON)"
[ -d "$DIR" ] || die "нет каталога шаблонов: $DIR"

# --- какие файлы берём ----------------------------------------------------------
FILES=()
if [ ${#WANT[@]} -gt 0 ]; then
  for id in "${WANT[@]}"; do
    [ -f "$DIR/$id.json" ] || die "нет шаблона «$id» в $DIR"
    FILES+=("$DIR/$id.json")
  done
else
  for f in "$DIR"/*.json; do FILES+=("$f"); done
fi
[ ${#FILES[@]} -gt 0 ] || die "не найдено ни одного шаблона в $DIR"

# --- что уже лежит в БД ---------------------------------------------------------
PSQL=(psql -v ON_ERROR_STOP=1 -q -d "$DB")
have=""
have="$("${PSQL[@]}" -tAc "SELECT count(*) FROM reports.report_templates" 2>/dev/null)" \
  || die "не удалось прочитать reports.report_templates в базе $DB (схема создана? db/init.sh)"

if [ "$LIST" = "1" ]; then
  echo "== файлы ($DIR): ${#FILES[@]}"
  for f in "${FILES[@]}"; do
    python3 - "$f" <<'PY'
import json,sys
d=json.load(open(sys.argv[1],encoding='utf-8'))
print(f"  {d.get('id','?'):26} {d.get('title','')[:60]}")
PY
  done
  echo "== база $DB: шаблонов $have"
  "${PSQL[@]}" -tAc "SELECT '  ' || id || ' — ' || title FROM reports.report_templates ORDER BY id"
  exit 0
fi

# --- проверка шаблонов ----------------------------------------------------------
if [ "$VALIDATE" = "1" ]; then
  if command -v node >/dev/null && [ -f "$ROOT/frontend/scripts/validate.mjs" ]; then
    echo "== валидация: ${#FILES[@]} шаблон(ов)"
    ( cd "$ROOT/frontend" && node scripts/validate.mjs "${FILES[@]}" ) \
      || die "шаблоны не прошли валидацию — загрузка отменена"
  else
    echo "ВНИМАНИЕ: node/валидатор не найдены — шаблоны не проверены (--no-validate для явного пропуска)"
  fi
fi

# --- сборка SQL -----------------------------------------------------------------
ARGS=()
SQL=$'BEGIN;\n'
INDEX=0
for f in "${FILES[@]}"; do
  INDEX=$((INDEX + 1))
  read -r id title < <(python3 - "$f" <<'PY'
import json,sys
d=json.load(open(sys.argv[1],encoding='utf-8'))
print(d.get('id',''), (d.get('title','') or '').replace('\n',' '))
PY
)
  [ -n "$id" ] || die "в $f нет id"
  [ -n "$title" ] || title="$id"

  ARGS+=(-v "id$INDEX=$id" -v "title$INDEX=$title" -v "body$INDEX=$(cat "$f")")
  SQL+="-- $id"$'\n'
  # прежнее тело — в историю, как делает публикация через админку
  SQL+="INSERT INTO reports.report_templates_history (report_id, title, body, saved_by)
      SELECT id, title, body, 'script' FROM reports.report_templates WHERE id = :'id$INDEX';
"
  SQL+="INSERT INTO reports.report_templates (id, title, body, updated_at)
      VALUES (:'id$INDEX', :'title$INDEX', :'body$INDEX'::jsonb, now())
      ON CONFLICT (id) DO UPDATE
        SET title = EXCLUDED.title,
            body = EXCLUDED.body,
            updated_at = now(),
            draft = NULL,
            draft_updated_at = NULL;
"
  SQL+="INSERT INTO reports.audit_log (actor, action, object_type, object_id, details)
      VALUES ('script', 'template.load', 'template', :'id$INDEX',
              jsonb_build_object('source', 'db/load-examples.sh', 'file', :'id$INDEX'));"$'\n'
done
SQL+=$'COMMIT;\n'

if [ "$DRY" = "1" ]; then
  echo "== план (ничего не меняется)"
  echo "  база: $DB, шаблонов в ней сейчас: $have"
  for f in "${FILES[@]}"; do
    python3 - "$f" <<'PY'
import json,sys
d=json.load(open(sys.argv[1],encoding='utf-8'))
print(f"  загрузить: {d.get('id','?'):26} {d.get('title','')[:60]}")
PY
  done
  exit 0
fi

# --- загрузка -------------------------------------------------------------------
echo "== загрузка в $DB: ${#FILES[@]} шаблон(ов), роль ${PGUSER:-по умолчанию}"
printf '%s' "$SQL" | "${PSQL[@]}" "${ARGS[@]}" \
  || die "загрузка не удалась — транзакция откатана, база не изменилась"

after="$("${PSQL[@]}" -tAc "SELECT count(*) FROM reports.report_templates")"
history="$("${PSQL[@]}" -tAc "SELECT count(*) FROM reports.report_templates_history")"
echo "   шаблонов в базе: $after (было $have), записей истории: $history"
echo "OK: примеры загружены. Сервер читает шаблоны на старте — перезапустите его, если он работает."
