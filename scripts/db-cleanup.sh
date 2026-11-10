#!/usr/bin/env bash
# Чистка мусора в БД после прогонов и демонстраций.
#   ./scripts/db-cleanup.sh            — убрать сироты и истёкшие сессии
#   ./scripts/db-cleanup.sh --dry-run  — только показать, что будет удалено
#
# Что удаляется:
#   * история версий шаблонов, у которых самого шаблона уже нет (e2e создаёт и удаляет
#     отчёты, а снимки остаются: FK между историей и шаблоном нет по дизайну ADR-26);
#   * истёкшие сессии (ADR-34).
#
# Что НЕ удаляется намеренно: журнал аудита, подписки и их попытки — это история работы,
# а не мусор; шаблоны и пользователей скрипт не трогает вообще.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
. "$ROOT/scripts/env.sh"

DRY=0
[ "${1:-}" = "--dry-run" ] && DRY=1
PSQL=(psql -v ON_ERROR_STOP=1 -q)

echo "== база $PGDATABASE ($PGHOST:$PGPORT), $([ "$DRY" = 1 ] && echo 'просмотр' || echo 'удаление')"

ORPHANS="$("${PSQL[@]}" -tAc "SELECT count(*) FROM reports.report_templates_history h
  WHERE NOT EXISTS (SELECT 1 FROM reports.report_templates t WHERE t.id = h.report_id)")"
EXPIRED="$("${PSQL[@]}" -tAc "SELECT count(*) FROM reports.sessions WHERE expires_at < now()")"

echo "   история без шаблона: $ORPHANS записей"
echo "   истёкшие сессии:     $EXPIRED"

if [ "$DRY" = 1 ]; then
  "${PSQL[@]}" -tAc "SELECT h.report_id || ' — ' || count(*) || ' версий'
    FROM reports.report_templates_history h
    WHERE NOT EXISTS (SELECT 1 FROM reports.report_templates t WHERE t.id = h.report_id)
    GROUP BY h.report_id ORDER BY 1"
  exit 0
fi

if [ "$ORPHANS" -gt 0 ]; then
  "${PSQL[@]}" -c "DELETE FROM reports.report_templates_history h
    WHERE NOT EXISTS (SELECT 1 FROM reports.report_templates t WHERE t.id = h.report_id)"
fi
if [ "$EXPIRED" -gt 0 ]; then
  "${PSQL[@]}" -c "DELETE FROM reports.sessions WHERE expires_at < now()"
fi

LEFT="$("${PSQL[@]}" -tAc "SELECT count(*) FROM reports.report_templates_history h
  WHERE NOT EXISTS (SELECT 1 FROM reports.report_templates t WHERE t.id = h.report_id)")"
echo "готово: удалено $ORPHANS версий-сирот, $EXPIRED сессий; осталось сирот: $LEFT"
