-- Схема данных для отчётов. Синтетика (ADR-16). Идемпотентно: DROP + CREATE.
DROP TABLE IF EXISTS sales;
DROP TABLE IF EXISTS finance;
DROP TABLE IF EXISTS ops;

-- Денежные величины — целые минорные единицы (копейки), ADR-8.
CREATE TABLE sales (
    id          bigint PRIMARY KEY,
    order_date  date    NOT NULL,
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
