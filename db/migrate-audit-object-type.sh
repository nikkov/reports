#!/usr/bin/env bash
# Миграция журнала (ADR-119): у старых записей open_report/export_report
# object_type = 'log', потому что событие писалось до ADR-111. Приводим к 'report',
# чтобы колонка «Объект» в админке и фильтры показывали одно и то же для всех строк.
#
# Скрипт идемпотентен: повторный запуск ничего не меняет.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
. "$ROOT/scripts/env.sh"

psql -v ON_ERROR_STOP=1 -c "
UPDATE reports.audit_log
   SET object_type = 'report'
 WHERE object_type = 'log'
   AND action IN ('open_report', 'export_report');
" >/dev/null

changed=$(psql -tAc "
SELECT count(*) FROM reports.audit_log
 WHERE object_type = 'log' AND action IN ('open_report', 'export_report');
")
echo "миграция журнала: строк с object_type='log' осталось: $changed"
