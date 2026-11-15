-- Миграция журнала (ADR-119): события отчётов до ADR-111 писались с object_type = 'log'.
-- Приводим их к 'report', чтобы колонка «Объект» и фильтры в админке показывали одно и то же.
-- Идемпотентно: повторный прогон не меняет ни одной строки.
UPDATE reports.audit_log
   SET object_type = 'report'
 WHERE object_type = 'log'
   AND action IN ('open_report', 'export_report');
