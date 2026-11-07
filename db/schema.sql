-- Схема данных для отчётов. Синтетика (ADR-16). Идемпотентно: DROP + CREATE.
DROP TABLE IF EXISTS report_templates;
DROP TABLE IF EXISTS sales;
DROP TABLE IF EXISTS finance;
DROP TABLE IF EXISTS ops;
DROP TABLE IF EXISTS regions;

-- Денежные величины — целые минорные единицы (копейки), ADR-8.
-- Справочник для параметров-списков (ID → NAME), ADR-22.
CREATE TABLE regions (
    id   integer PRIMARY KEY,
    name text    NOT NULL UNIQUE
);

CREATE TABLE sales (
    id          bigint PRIMARY KEY,
    order_date  date    NOT NULL,
    region_id   integer REFERENCES regions (id),
    region      text,
    category    text,
    product     text    NOT NULL,
    customer    text    NOT NULL,
    qty         integer NOT NULL,
    amount      bigint  NOT NULL,
    profit      bigint  NOT NULL
);

CREATE INDEX sales_order_date_idx ON sales (order_date);
CREATE INDEX sales_region_idx     ON sales (region);
CREATE INDEX sales_region_id_idx  ON sales (region_id);
CREATE INDEX sales_category_idx   ON sales (category);

CREATE TABLE finance (
    id          bigint PRIMARY KEY,
    period      date   NOT NULL,
    cost_center text,
    account     text   NOT NULL,
    plan_amount bigint NOT NULL,
    fact_amount bigint NOT NULL
);

CREATE INDEX finance_period_idx ON finance (period);

-- История версий шаблонов (ADR-26): append-only снапшоты при сохранении и удалении.
CREATE TABLE IF NOT EXISTS report_templates_history (
    id         bigserial   PRIMARY KEY,
    report_id  text        NOT NULL,
    title      text,
    body       jsonb       NOT NULL,
    saved_at   timestamptz NOT NULL DEFAULT now(),
    saved_by   text        NOT NULL DEFAULT 'admin'
);

CREATE INDEX IF NOT EXISTS report_templates_history_report_idx
    ON report_templates_history (report_id, id DESC);

CREATE TABLE ops (
    id       bigint  PRIMARY KEY,
    day      date    NOT NULL,
    service  text    NOT NULL,
    team     text,
    requests integer NOT NULL,
    failures integer NOT NULL,
    avg_ms   integer NOT NULL
);

CREATE INDEX ops_day_idx ON ops (day);

-- Шаблоны отчётов хранятся в БД (ADR-21): id, заголовок и тело шаблона как jsonb.
CREATE TABLE report_templates (
    id               text        PRIMARY KEY,
    title            text        NOT NULL,
    body             jsonb       NOT NULL,        -- опубликованная версия
    updated_at       timestamptz NOT NULL DEFAULT now(),
    draft            jsonb,                       -- черновик правки (ADR-31)
    draft_updated_at timestamptz
);
-- Пользователи и сессии (ADR-34). Пароль хранится как PBKDF2-HMAC-SHA256 с солью.
CREATE TABLE IF NOT EXISTS users (
    id            serial      PRIMARY KEY,
    login         text        NOT NULL UNIQUE,
    password_salt text        NOT NULL,
    password_hash text        NOT NULL,
    role          text        NOT NULL CHECK (role IN ('admin', 'author', 'viewer')),
    created_at    timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS sessions (
    token      text        PRIMARY KEY,
    user_id    integer     NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at timestamptz NOT NULL DEFAULT now(),
    expires_at timestamptz NOT NULL
);

CREATE INDEX IF NOT EXISTS sessions_expires_idx ON sessions (expires_at);
