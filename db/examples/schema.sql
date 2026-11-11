-- ПРИМЕРЫ: синтетические данные для демонстрационных отчётов.
-- Схема reports_examples. Ядро продукта живёт отдельно (db/core.sql, схема reports) —
-- примеры можно не ставить: каталог и админка работают, отчёты примеров скажут,
-- что таблиц нет. Схема намеренно отдельная: шаблоны ссылаются на неё явно
-- (reports_examples.sales), и видно, где ядро, а где демо-данные.
--
-- Идемпотентно по структуре; данные грузит db/examples/seed.sql (DROP + заливка).

CREATE SCHEMA IF NOT EXISTS reports_examples;

CREATE TABLE IF NOT EXISTS reports_examples.regions (
    id   integer PRIMARY KEY,
    name text    NOT NULL UNIQUE
);

CREATE TABLE IF NOT EXISTS reports_examples.sales (
    id          bigint PRIMARY KEY,
    order_date  date    NOT NULL,
    region_id   integer REFERENCES reports_examples.regions (id),
    region      text,
    category    text,
    product     text    NOT NULL,
    customer    text    NOT NULL,
    qty         integer NOT NULL,
    amount      bigint  NOT NULL,
    profit      bigint  NOT NULL
);

CREATE INDEX IF NOT EXISTS sales_order_date_idx ON reports_examples.sales (order_date);
CREATE INDEX IF NOT EXISTS sales_region_idx     ON reports_examples.sales (region);
CREATE INDEX IF NOT EXISTS sales_region_id_idx  ON reports_examples.sales (region_id);
CREATE INDEX IF NOT EXISTS sales_category_idx   ON reports_examples.sales (category);

CREATE TABLE IF NOT EXISTS reports_examples.finance (
    id          bigint PRIMARY KEY,
    period      date   NOT NULL,
    cost_center text,
    account     text   NOT NULL,
    plan_amount bigint NOT NULL,
    fact_amount bigint NOT NULL
);

CREATE INDEX IF NOT EXISTS finance_period_idx ON reports_examples.finance (period);

CREATE TABLE IF NOT EXISTS reports_examples.ops (
    id       bigint  PRIMARY KEY,
    day      date    NOT NULL,
    service  text    NOT NULL,
    team     text,
    requests integer NOT NULL,
    failures integer NOT NULL,
    avg_ms   integer NOT NULL
);

CREATE INDEX IF NOT EXISTS ops_day_idx ON reports_examples.ops (day);

-- Время события (2026-02-11): параметры вида datetime фильтруют по нему.
-- ADD COLUMN IF NOT EXISTS нужен для уже развёрнутых баз: CREATE TABLE её не добавит.
ALTER TABLE reports_examples.ops ADD COLUMN IF NOT EXISTS logged_at timestamp;
UPDATE reports_examples.ops SET logged_at = day::timestamp WHERE logged_at IS NULL;
ALTER TABLE reports_examples.ops ALTER COLUMN logged_at SET NOT NULL;
CREATE INDEX IF NOT EXISTS ops_logged_at_idx ON reports_examples.ops (logged_at);
