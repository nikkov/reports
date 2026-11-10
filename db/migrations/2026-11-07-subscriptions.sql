-- Рассылка отчётов по расписанию (ADR-46). Идемпотентно: применяется на работающей БД.
CREATE SCHEMA IF NOT EXISTS reports;

CREATE TABLE IF NOT EXISTS reports.report_subscriptions (
    id           bigserial PRIMARY KEY,
    report_id    text        NOT NULL,
    title        text        NOT NULL,
    cron         text        NOT NULL,
    params       jsonb       NOT NULL DEFAULT '{}'::jsonb,
    format       text        NOT NULL DEFAULT 'csv',
    recipients   text[]      NOT NULL,
    enabled      boolean     NOT NULL DEFAULT true,
    next_run_at  timestamptz,
    last_run_at  timestamptz,
    created_at   timestamptz NOT NULL DEFAULT now(),
    updated_at   timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS report_subscriptions_due_idx
    ON reports.report_subscriptions (next_run_at) WHERE enabled;

CREATE TABLE IF NOT EXISTS reports.subscription_runs (
    id              bigserial PRIMARY KEY,
    subscription_id bigint      NOT NULL REFERENCES reports.report_subscriptions (id) ON DELETE CASCADE,
    started_at      timestamptz NOT NULL DEFAULT now(),
    finished_at     timestamptz,
    ok              boolean     NOT NULL,
    error           text,
    rows            integer,
    recipients      integer,
    duration_ms     integer
);

CREATE INDEX IF NOT EXISTS subscription_runs_sub_idx ON reports.subscription_runs (subscription_id, started_at DESC);
