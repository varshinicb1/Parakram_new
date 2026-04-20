-- Parakram community driver marketplace schema
-- Phase 5A: User-submitted firmware drivers with moderation + ratings

-- community_drivers: user-submitted firmware drivers
CREATE TABLE IF NOT EXISTS community_drivers (
    id               UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    author_id        TEXT NOT NULL,                -- Supabase user UUID
    name             TEXT NOT NULL UNIQUE,         -- snake_case, e.g. "drv_my_sensor"
    display_name     TEXT NOT NULL,
    description      TEXT NOT NULL,
    version          TEXT NOT NULL DEFAULT '1.0.0',
    driver_type      TEXT NOT NULL CHECK (driver_type IN ('sensor','actuator','display','combo')),
    bus_types        TEXT[] NOT NULL DEFAULT '{}', -- ['i2c','spi','gpio',...]
    capabilities     TEXT[] NOT NULL DEFAULT '{}', -- ['CAP_TEMPERATURE','CAP_HUMIDITY',...]
    source_code      TEXT NOT NULL,                -- full C source, max 64KB enforced by app
    status           TEXT NOT NULL DEFAULT 'pending'
                       CHECK (status IN ('pending','approved','rejected','flagged','withdrawn')),
    rejection_reason TEXT,
    validation_json  JSONB,                        -- stored result of static analysis
    downloads        INTEGER NOT NULL DEFAULT 0,
    stars_total      INTEGER NOT NULL DEFAULT 0,
    stars_count      INTEGER NOT NULL DEFAULT 0,
    created_at       TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at       TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_cd_status     ON community_drivers (status);
CREATE INDEX IF NOT EXISTS idx_cd_author     ON community_drivers (author_id);
CREATE INDEX IF NOT EXISTS idx_cd_type       ON community_drivers (driver_type);
CREATE INDEX IF NOT EXISTS idx_cd_created    ON community_drivers (created_at DESC);
CREATE INDEX IF NOT EXISTS idx_cd_downloads  ON community_drivers (downloads DESC);

-- driver_ratings: one row per (user, driver)
CREATE TABLE IF NOT EXISTS driver_ratings (
    user_id    TEXT NOT NULL,
    driver_id  UUID NOT NULL REFERENCES community_drivers(id) ON DELETE CASCADE,
    stars      INTEGER NOT NULL CHECK (stars BETWEEN 1 AND 5),
    review     TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (user_id, driver_id)
);

-- driver_installs: users that have installed a community driver
CREATE TABLE IF NOT EXISTS driver_installs (
    user_id      TEXT NOT NULL,
    driver_id    UUID NOT NULL REFERENCES community_drivers(id) ON DELETE CASCADE,
    installed_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (user_id, driver_id)
);
CREATE INDEX IF NOT EXISTS idx_di_user ON driver_installs (user_id);

-- RLS
ALTER TABLE community_drivers  ENABLE ROW LEVEL SECURITY;
ALTER TABLE driver_ratings     ENABLE ROW LEVEL SECURITY;
ALTER TABLE driver_installs    ENABLE ROW LEVEL SECURITY;

-- Anyone can read approved drivers (public catalog)
DROP POLICY IF EXISTS cd_read_approved ON community_drivers;
CREATE POLICY cd_read_approved ON community_drivers
    FOR SELECT USING (status = 'approved' OR auth.uid()::text = author_id);

-- Authors can withdraw their own pending drivers
DROP POLICY IF EXISTS cd_author_update ON community_drivers;
CREATE POLICY cd_author_update ON community_drivers
    FOR UPDATE USING (auth.uid()::text = author_id);

-- Users can manage their own ratings and installs
DROP POLICY IF EXISTS ratings_self ON driver_ratings;
CREATE POLICY ratings_self ON driver_ratings
    FOR ALL USING (auth.uid()::text = user_id);

DROP POLICY IF EXISTS installs_self ON driver_installs;
CREATE POLICY installs_self ON driver_installs
    FOR ALL USING (auth.uid()::text = user_id);

-- Service role bypasses RLS for admin operations
