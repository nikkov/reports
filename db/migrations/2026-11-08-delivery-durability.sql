-- Устойчивость отправки рассылки (ADR-62).
-- Попытка пишется в журнал до отправки, поэтому незавершённые строки означают обрыв
-- процесса. Их ищем по частичному индексу: finished_at IS NULL.
CREATE SCHEMA IF NOT EXISTS reports;

CREATE INDEX IF NOT EXISTS subscription_runs_open_idx
    ON reports.subscription_runs (started_at) WHERE finished_at IS NULL;

COMMENT ON COLUMN reports.subscription_runs.finished_at IS
    'NULL — попытка ещё идёт или процесс упал во время отправки';
