-- Роль только-чтения для данных отчётов (ADR-33).
-- Создание и пароль выполняет scripts/db-ro.sh; здесь — только права.
-- Роль видит таблицы, но не может писать: INSERT/UPDATE/DELETE/DDL запрещены.

GRANT CONNECT ON DATABASE reporting TO reporting_ro;
GRANT USAGE ON SCHEMA public TO reporting_ro;
GRANT SELECT ON ALL TABLES IN SCHEMA public TO reporting_ro;

-- чтобы новые таблицы (например, после миграций) тоже были доступны только на чтение
ALTER DEFAULT PRIVILEGES IN SCHEMA public GRANT SELECT ON TABLES TO reporting_ro;

-- STATEMENT_TIMEOUT уже задаётся в DSN бэкенда, но ролью фиксируем верхнюю границу
ALTER ROLE reporting_ro SET statement_timeout = '2s';
ALTER ROLE reporting_ro SET default_transaction_read_only = on;
