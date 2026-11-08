-- Устойчивость отправки рассылки (ADR-62).
-- Попытка пишется в журнал до отправки, поэтому незавершённые строки означают обрыв
-- процесса. Их ищем по частичному индексу: finished_at IS NULL.
CREATE INDEX IF NOT EXISTS subscription_runs_open_idx
    ON subscription_runs (started_at) WHERE finished_at IS NULL;

COMMENT ON COLUMN subscription_runs.finished_at IS
    'NULL — попытка ещё идёт или процесс упал во время отправки';
