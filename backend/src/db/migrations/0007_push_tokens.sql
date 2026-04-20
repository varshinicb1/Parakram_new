-- Push notification device tokens.
--
-- One row per (user, platform).  When the OS rotates the token the app
-- re-registers and the row is updated in-place via the ON CONFLICT clause.

CREATE TABLE IF NOT EXISTS push_tokens (
    user_id    UUID        NOT NULL,
    platform   TEXT        NOT NULL CHECK (platform IN ('android', 'ios')),
    token      TEXT        NOT NULL,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (user_id, platform)
);
