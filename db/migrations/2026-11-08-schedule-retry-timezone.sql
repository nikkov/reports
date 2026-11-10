-- Часовой пояс расписания и повторные попытки отправки (ADR-60, ADR-61).
-- Идемпотентно: применяется db/init.sh к любой базе.

CREATE SCHEMA IF NOT EXISTS reports;

ALTER TABLE reports.report_subscriptions
    ADD COLUMN IF NOT EXISTS timezone text NOT NULL DEFAULT 'UTC';

ALTER TABLE reports.report_subscriptions
    ADD COLUMN IF NOT EXISTS retry_count integer NOT NULL DEFAULT 0;

ALTER TABLE reports.report_subscriptions
    ADD COLUMN IF NOT EXISTS retry_at timestamptz;

ALTER TABLE reports.subscription_runs
    ADD COLUMN IF NOT EXISTS attempt integer NOT NULL DEFAULT 1;

-- планировщик выбирает просроченные повторы, поэтому нужен индекс по retry_at
CREATE INDEX IF NOT EXISTS report_subscriptions_retry_idx
    ON reports.report_subscriptions (retry_at) WHERE retry_at IS NOT NULL;

-- допустимые пояса берутся из pg_timezone_names; значение проверяется в API
COMMENT ON COLUMN reports.report_subscriptions.timezone IS 'IANA-пояс расписания, например Europe/Moscow';
COMMENT ON COLUMN reports.report_subscriptions.retry_at IS 'когда повторить неудачную отправку; NULL — повторов нет';
