-- Страховки роли только-чтения (ADR-33). Требуют прав суперпользователя (или членства
-- в роли): на обычной dev-базе скрипт их пропускает с предупреждением.
-- Сам бэкенд уже ставит statement_timeout в DSN и ходит в пул только-чтения.

ALTER ROLE reporting_ro SET statement_timeout = '2s';
ALTER ROLE reporting_ro SET default_transaction_read_only = on;
