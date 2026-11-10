-- Управление пользователями (ADR-50): отключение без удаления и время изменения.
CREATE SCHEMA IF NOT EXISTS reports;

ALTER TABLE reports.users ADD COLUMN IF NOT EXISTS enabled boolean NOT NULL DEFAULT true;
ALTER TABLE reports.users ADD COLUMN IF NOT EXISTS updated_at timestamptz NOT NULL DEFAULT now();
CREATE INDEX IF NOT EXISTS users_login_idx ON reports.users (login);
