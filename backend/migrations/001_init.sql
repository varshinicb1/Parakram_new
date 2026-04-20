-- ============================================================================
-- Parakram Database Schema — Supabase PostgreSQL
-- Run this in your Supabase SQL Editor (Dashboard → SQL Editor → New Query)
-- ============================================================================

-- 1. Users (Supabase Auth handles this, but we track extra metadata)
CREATE TABLE IF NOT EXISTS user_profiles (
    user_id TEXT PRIMARY KEY,
    username TEXT UNIQUE NOT NULL,
    email TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    role TEXT DEFAULT 'user',
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW()
);

-- 2. Subscriptions (UPI-based)
CREATE TABLE IF NOT EXISTS subscriptions (
    user_id TEXT PRIMARY KEY,
    plan_tier TEXT DEFAULT 'free',
    status TEXT DEFAULT 'active',
    upi_utr TEXT,
    payment_verified BOOLEAN DEFAULT false,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW(),
    expires_at TIMESTAMPTZ
);

-- Patch existing subscriptions table (if it was created by old Stripe setup)
ALTER TABLE subscriptions ADD COLUMN IF NOT EXISTS upi_utr TEXT;
ALTER TABLE subscriptions ADD COLUMN IF NOT EXISTS payment_verified BOOLEAN DEFAULT false;
ALTER TABLE subscriptions ADD COLUMN IF NOT EXISTS expires_at TIMESTAMPTZ;
ALTER TABLE subscriptions ADD COLUMN IF NOT EXISTS plan_tier TEXT DEFAULT 'free';
ALTER TABLE subscriptions ADD COLUMN IF NOT EXISTS status TEXT DEFAULT 'active';

-- 3. Payment Claims (UPI UTR verification)
CREATE TABLE IF NOT EXISTS payment_claims (
    id BIGSERIAL PRIMARY KEY,
    user_id TEXT NOT NULL,
    upi_utr TEXT UNIQUE NOT NULL,
    amount_inr INTEGER NOT NULL,
    status TEXT DEFAULT 'pending', -- pending, approved, rejected
    submitted_at TIMESTAMPTZ DEFAULT NOW(),
    reviewed_at TIMESTAMPTZ,
    reviewed_by TEXT
);

-- 4. Usage Counters (per-user per-month)
CREATE TABLE IF NOT EXISTS usage_counters (
    user_id TEXT NOT NULL,
    period_start TIMESTAMPTZ NOT NULL,
    llm_intents INTEGER DEFAULT 0,
    compiles INTEGER DEFAULT 0,
    deploys INTEGER DEFAULT 0,
    devices_active INTEGER DEFAULT 0,
    updated_at TIMESTAMPTZ DEFAULT NOW(),
    PRIMARY KEY (user_id, period_start)
);

-- 5. Projects
CREATE TABLE IF NOT EXISTS projects (
    id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL,
    name TEXT NOT NULL,
    description TEXT DEFAULT '',
    category TEXT DEFAULT 'General',
    status TEXT DEFAULT 'draft',
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW(),
    ir_document JSONB,
    config JSONB DEFAULT '{}',
    bytecode_size INTEGER DEFAULT 0,
    deploy_count INTEGER DEFAULT 0,
    device_id TEXT,
    last_deployed_at TIMESTAMPTZ,
    template_id TEXT
);

-- 6. Devices
CREATE TABLE IF NOT EXISTS devices (
    id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL,
    name TEXT NOT NULL,
    board_type TEXT DEFAULT 'esp32s3',
    status TEXT DEFAULT 'offline',
    firmware_version TEXT,
    last_seen_at TIMESTAMPTZ,
    created_at TIMESTAMPTZ DEFAULT NOW()
);

-- 7. Community Drivers (Marketplace)
CREATE TABLE IF NOT EXISTS community_drivers (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    author_id TEXT NOT NULL,
    name TEXT UNIQUE NOT NULL,
    display_name TEXT NOT NULL,
    description TEXT NOT NULL,
    version TEXT NOT NULL,
    driver_type TEXT NOT NULL,
    bus_types TEXT[] DEFAULT '{}',
    capabilities TEXT[] DEFAULT '{}',
    source_code TEXT NOT NULL,
    status TEXT DEFAULT 'pending', -- pending, approved, rejected
    rejection_reason TEXT,
    downloads INTEGER DEFAULT 0,
    stars_total INTEGER DEFAULT 0,
    stars_count INTEGER DEFAULT 0,
    validation_json JSONB,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW()
);

-- 8. Driver Ratings
CREATE TABLE IF NOT EXISTS driver_ratings (
    user_id TEXT NOT NULL,
    driver_id UUID NOT NULL,
    stars INTEGER NOT NULL CHECK (stars BETWEEN 1 AND 5),
    review TEXT,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    PRIMARY KEY (user_id, driver_id)
);

-- 9. Driver Installs
CREATE TABLE IF NOT EXISTS driver_installs (
    user_id TEXT NOT NULL,
    driver_id UUID NOT NULL,
    installed_at TIMESTAMPTZ DEFAULT NOW(),
    PRIMARY KEY (user_id, driver_id)
);

-- 10. Notification Tokens (FCM/APNs)
CREATE TABLE IF NOT EXISTS notification_tokens (
    user_id TEXT NOT NULL,
    token TEXT NOT NULL,
    platform TEXT DEFAULT 'fcm',
    created_at TIMESTAMPTZ DEFAULT NOW(),
    PRIMARY KEY (user_id, token)
);

-- ============================================================================
-- INDEXES
-- ============================================================================

CREATE INDEX IF NOT EXISTS idx_projects_user ON projects(user_id);
CREATE INDEX IF NOT EXISTS idx_devices_user ON devices(user_id);
CREATE INDEX IF NOT EXISTS idx_payment_claims_status ON payment_claims(status);
CREATE INDEX IF NOT EXISTS idx_payment_claims_user ON payment_claims(user_id);
CREATE INDEX IF NOT EXISTS idx_community_drivers_status ON community_drivers(status);
CREATE INDEX IF NOT EXISTS idx_usage_counters_period ON usage_counters(user_id, period_start);

-- ============================================================================
-- SEED: Default admin user (password: parakram-admin)
-- ============================================================================

INSERT INTO user_profiles (user_id, username, email, password_hash, role)
VALUES (
    'admin-001',
    'admin',
    'varshinicb@vidyuthlabs.co.in',
    '$argon2id$v=19$m=19456,t=2,p=1$c2FsdHlzYWx0$demo_hash_replace_on_first_login',
    'admin'
) ON CONFLICT (user_id) DO NOTHING;

-- Seed admin subscription (only columns guaranteed to exist)
INSERT INTO subscriptions (user_id, plan_tier, status)
VALUES ('admin-001', 'maker', 'active')
ON CONFLICT (user_id) DO UPDATE SET plan_tier = 'maker', status = 'active';

-- Set payment_verified separately (safe if column was just added)
UPDATE subscriptions SET payment_verified = true WHERE user_id = 'admin-001';
