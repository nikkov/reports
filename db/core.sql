-- ЯДРО сервера отчётов: всё, что нужно продукту для работы.
-- Схема reports. Демо-данные живут отдельно (db/examples/schema.sql, схема reports_examples):
-- ядро можно поставить в чистую базу без синтетики — шаблоны отчётов тогда просто не найдут
-- своих таблиц, но каталог, пользователи, рассылка и журнал работают.
--
-- Идемпотентно: CREATE ... IF NOT EXISTS. Полное пересоздание — db/init.sh (dev-база).

CREATE SCHEMA IF NOT EXISTS reports;

-- Шаблоны отчётов хранятся в БД (ADR-21): тело — jsonb, черновик рядом (ADR-31).
CREATE TABLE IF NOT EXISTS reports.report_templates (
    id               text        PRIMARY KEY,
    title            text        NOT NULL,
    body             jsonb       NOT NULL,        -- опубликованная версия
    updated_at       timestamptz NOT NULL DEFAULT now(),
    draft            jsonb,                       -- черновик правки (ADR-31)
    draft_updated_at timestamptz
);

-- История версий шаблонов (ADR-26): append-only снапшоты при сохранении и удалении.
CREATE TABLE IF NOT EXISTS reports.report_templates_history (
    id         bigserial   PRIMARY KEY,
    report_id  text        NOT NULL,
    title      text,
    body       jsonb       NOT NULL,
    saved_at   timestamptz NOT NULL DEFAULT now(),
    saved_by   text        NOT NULL DEFAULT 'admin'
);

CREATE INDEX IF NOT EXISTS report_templates_history_report_idx
    ON reports.report_templates_history (report_id, id DESC);

-- Пользователи и сессии (ADR-34). Пароль — PBKDF2-HMAC-SHA256 с солью.
CREATE TABLE IF NOT EXISTS reports.users (
    id            serial      PRIMARY KEY,
    login         text        NOT NULL UNIQUE,
    password_salt text        NOT NULL,
    password_hash text        NOT NULL,
    role          text        NOT NULL CHECK (role IN ('admin', 'author', 'viewer')),
    created_at    timestamptz NOT NULL DEFAULT now(),
    enabled       boolean     NOT NULL DEFAULT true,
    updated_at    timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS users_login_idx ON reports.users (login);

-- Доменные учётные записи (ADR-93): логин сравнивается без учёта регистра, поэтому
-- CORP\ivanov и corp\Ivanov — одна и та же учётная запись, дубликат не создаётся.
CREATE UNIQUE INDEX IF NOT EXISTS users_login_lower_key ON reports.users (lower(login));

CREATE TABLE IF NOT EXISTS reports.sessions (
    token      text        PRIMARY KEY,
    user_id    integer     NOT NULL REFERENCES reports.users(id) ON DELETE CASCADE,
    created_at timestamptz NOT NULL DEFAULT now(),
    expires_at timestamptz NOT NULL
);

CREATE INDEX IF NOT EXISTS sessions_expires_idx ON reports.sessions (expires_at);

-- Журнал действий (ADR-52).
CREATE TABLE IF NOT EXISTS reports.audit_log (
    id          bigserial   PRIMARY KEY,
    at          timestamptz NOT NULL DEFAULT now(),
    actor       text        NOT NULL,          -- логин или system
    action      text        NOT NULL,          -- template.publish, user.role, subscription.run …
    object_type text        NOT NULL,
    object_id   text        NOT NULL,
    details     jsonb       NOT NULL DEFAULT '{}'::jsonb,
    ip          text
);

CREATE INDEX IF NOT EXISTS audit_log_at_idx     ON reports.audit_log (at DESC);
CREATE INDEX IF NOT EXISTS audit_log_action_idx ON reports.audit_log (action, at DESC);
CREATE INDEX IF NOT EXISTS audit_log_actor_idx  ON reports.audit_log (actor, at DESC);

-- Рассылка отчётов (ADR-46, ADR-60, ADR-61).
CREATE TABLE IF NOT EXISTS reports.report_subscriptions (
    id           bigserial   PRIMARY KEY,
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
    updated_at   timestamptz NOT NULL DEFAULT now(),
    timezone     text        NOT NULL DEFAULT 'UTC',
    retry_count  integer     NOT NULL DEFAULT 0,
    retry_at     timestamptz
);

CREATE INDEX IF NOT EXISTS report_subscriptions_due_idx
    ON reports.report_subscriptions (next_run_at) WHERE enabled;

CREATE INDEX IF NOT EXISTS report_subscriptions_retry_idx
    ON reports.report_subscriptions (retry_at) WHERE retry_at IS NOT NULL;

COMMENT ON COLUMN reports.report_subscriptions.timezone IS 'IANA-пояс расписания, например Europe/Moscow';
COMMENT ON COLUMN reports.report_subscriptions.retry_at IS 'когда повторить неудачную отправку; NULL — повторов нет';

CREATE TABLE IF NOT EXISTS reports.subscription_runs (
    id              bigserial   PRIMARY KEY,
    subscription_id bigint      NOT NULL REFERENCES reports.report_subscriptions (id) ON DELETE CASCADE,
    started_at      timestamptz NOT NULL DEFAULT now(),
    finished_at     timestamptz,
    ok              boolean     NOT NULL,
    error           text,
    rows            integer,
    recipients      integer,
    duration_ms     integer,
    attempt         integer     NOT NULL DEFAULT 1
);

CREATE INDEX IF NOT EXISTS subscription_runs_sub_idx
    ON reports.subscription_runs (subscription_id, started_at DESC);

CREATE INDEX IF NOT EXISTS subscription_runs_open_idx
    ON reports.subscription_runs (started_at) WHERE finished_at IS NULL;

COMMENT ON COLUMN reports.subscription_runs.finished_at IS
    'NULL — попытка ещё идёт или процесс упал во время отправки';
