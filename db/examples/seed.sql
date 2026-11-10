-- Детерминированные синтетические данные примеров (ADR-16, ADR-74).
-- Схема reports_examples; повторный запуск безопасен.
-- Значения получаются арифметикой от i, поэтому результат не зависит от random()/версии СУБД.
BEGIN;

TRUNCATE reports_examples.sales, reports_examples.finance, reports_examples.ops, reports_examples.regions RESTART IDENTITY;

INSERT INTO reports_examples.regions (id, name) VALUES
    (1, 'Север'), (2, 'Юг'), (3, 'Запад'), (4, 'Восток'), (5, 'Центр');

-- reports_examples.sales: 50 000 строк, период 2025-10-01 .. 2026-03-31 (182 дня)
INSERT INTO reports_examples.sales (id, order_date, region_id, region, category, product, customer, qty, amount, profit)
SELECT i,
       DATE '2025-10-01' + ((i * 17) % 182),
       (SELECT id FROM reports_examples.regions WHERE name = CASE WHEN i % 1009 = 0 THEN NULL
            ELSE (ARRAY['Север','Юг','Запад','Восток','Центр'])[1 + (i * 7) % 5] END),
       CASE WHEN i % 1009 = 0 THEN NULL
            ELSE (ARRAY['Север','Юг','Запад','Восток','Центр'])[1 + (i * 7) % 5] END,
       CASE WHEN i % 2113 = 0 THEN NULL
            ELSE (ARRAY['Электроника','Бытовая техника','Мебель','Аксессуары'])[1 + (i * 13) % 4] END,
       'Товар-'  || (1 + (i * 29) % 40),
       'Клиент-' || (1 + (i * 31) % 300),
       1 + (i * 3) % 25,
       CASE WHEN i % 997  = 0 THEN 0                        -- нулевой знаменатель для percent
            WHEN i      = 50000 THEN 999999999999            -- выброс, проверка точности
            ELSE 100 * (100 + (i * 37) % 900) END,
       CASE WHEN i % 997  = 0 THEN 0
            ELSE (100 * (100 + (i * 37) % 900)) / 4 - 100 * ((i * 11) % 50) END
FROM generate_series(1, 50000) AS g(i);

-- reports_examples.finance: 6 периодов x 12 ЦФ x 8 счетов = 576 строк
INSERT INTO reports_examples.finance (id, period, cost_center, account, plan_amount, fact_amount)
SELECT i,
       DATE '2025-10-01' + (((i * 5) % 6) * 30),
       CASE WHEN i % 97 = 0 THEN NULL ELSE 'ЦФ-' || (1 + (i * 7) % 12) END,
       'Счет-' || (1 + (i * 3) % 8),
       1000 * (500 + (i * 41) % 5000),
       1000 * (500 + (i * 43) % 5000)
FROM generate_series(1, 576) AS g(i);

-- reports_examples.ops: 90 дней x 6 сервисов = 540 строк
INSERT INTO reports_examples.ops (id, day, service, team, requests, failures, avg_ms)
SELECT i,
       DATE '2026-01-01' + ((i * 11) % 90),
       (ARRAY['api','web','etl','auth','billing','search'])[1 + (i * 5) % 6],
       CASE WHEN i % 53 = 0 THEN NULL ELSE 'Team-' || (1 + (i * 3) % 4) END,
       100 + (i * 37) % 900,
       (i * 7 + (i % 97)) % 12,
       50 + (i * 13) % 400
FROM generate_series(1, 540) AS g(i);

ANALYZE reports_examples.sales;
ANALYZE reports_examples.finance;
ANALYZE reports_examples.ops;

COMMIT;
