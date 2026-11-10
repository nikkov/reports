-- Роль только-чтения для данных отчётов (ADR-33, ADR-74): права на схему примеров.
-- Создание и пароль выполняет scripts/db-ro.sh; здесь — только GRANT'ы.
-- Роль видит примеры (reports_examples), но не ядро (reports) и не может писать.

GRANT CONNECT ON DATABASE reporting TO reporting_ro;
GRANT USAGE ON SCHEMA reports_examples TO reporting_ro;
GRANT SELECT ON ALL TABLES IN SCHEMA reports_examples TO reporting_ro;

-- чтобы новые таблицы (например, после миграций) тоже были доступны только на чтение
ALTER DEFAULT PRIVILEGES IN SCHEMA reports_examples GRANT SELECT ON TABLES TO reporting_ro;
