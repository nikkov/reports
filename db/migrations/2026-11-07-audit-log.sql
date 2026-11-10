-- Аудит действий (ADR-52): кто что сделал, когда и с какого адреса.
CREATE SCHEMA IF NOT EXISTS reports;

CREATE TABLE IF NOT EXISTS reports.audit_log (
    id          bigserial PRIMARY KEY,
    at          timestamptz NOT NULL DEFAULT now(),
    actor       text        NOT NULL,          -- логин или system
    action      text        NOT NULL,          -- template.publish, user.role, subscription.run …
    object_type text        NOT NULL,
    object_id   text        NOT NULL,
    details     jsonb       NOT NULL DEFAULT '{}'::jsonb,
    ip          text
);

CREATE INDEX IF NOT EXISTS audit_log_at_idx ON reports.audit_log (at DESC);
CREATE INDEX IF NOT EXISTS audit_log_action_idx ON reports.audit_log (action, at DESC);
CREATE INDEX IF NOT EXISTS audit_log_actor_idx ON reports.audit_log (actor, at DESC);
