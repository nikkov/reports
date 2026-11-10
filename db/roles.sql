-- Права эксплуатационных ролей (ADR-33, ADR-74, ADR-75). Кто чем пользуется:
--   reporting_owner — владелец схем и таблиц: DDL и миграции при развёртывании;
--   reporting_srv   — сам сервер: шаблоны, пользователи, сессии, рассылка, журнал (DML),
--                     плюс чтение примеров; DDL и создание ролей запрещены;
--   reporting_ro    — только чтение данных отчётов (reports_examples), без доступа к ядру.
-- Суперпользователь в эксплуатации не используется: он нужен лишь для установки и миграций.
--
-- Скрипт идемпотентен: GRANT можно повторять. Выполняется владельцем объектов
-- (reporting_owner) или суперпользователем при развёртывании.

-- --- сервер: ядро (DML без DDL) -------------------------------------------------
GRANT USAGE ON SCHEMA reports TO reporting_srv;
GRANT SELECT, INSERT, UPDATE, DELETE ON ALL TABLES IN SCHEMA reports TO reporting_srv;
-- bigserial: nextval/currval требуют USAGE, возврат значения — SELECT
GRANT USAGE, SELECT ON ALL SEQUENCES IN SCHEMA reports TO reporting_srv;

-- новые таблицы и последовательности ядра должны доставаться серверу автоматически
ALTER DEFAULT PRIVILEGES IN SCHEMA reports
    GRANT SELECT, INSERT, UPDATE, DELETE ON TABLES TO reporting_srv;
ALTER DEFAULT PRIVILEGES IN SCHEMA reports
    GRANT USAGE, SELECT ON SEQUENCES TO reporting_srv;

-- --- сервер: примеры (только чтение, шаблоны выполняют SELECT) -------------------
GRANT USAGE ON SCHEMA reports_examples TO reporting_srv;
GRANT SELECT ON ALL TABLES IN SCHEMA reports_examples TO reporting_srv;
ALTER DEFAULT PRIVILEGES IN SCHEMA reports_examples
    GRANT SELECT ON TABLES TO reporting_srv;

-- --- зритель: только данные отчётов ---------------------------------------------
GRANT USAGE ON SCHEMA reports_examples TO reporting_ro;
GRANT SELECT ON ALL TABLES IN SCHEMA reports_examples TO reporting_ro;

-- чтобы новые таблицы (например, после миграций) тоже были доступны только на чтение
ALTER DEFAULT PRIVILEGES IN SCHEMA reports_examples GRANT SELECT ON TABLES TO reporting_ro;
