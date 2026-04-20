-- Parakram billing schema
-- Phase 2B: Stripe subscriptions + per-user monthly usage counters

CREATE TABLE IF NOT EXISTS subscriptions (
    user_id                  TEXT PRIMARY KEY,
    stripe_customer_id       TEXT UNIQUE,
    stripe_subscription_id   TEXT UNIQUE,
    plan_tier                TEXT NOT NULL DEFAULT 'free'
                               CHECK (plan_tier IN ('free','hobby','pro','enterprise')),
    status                   TEXT NOT NULL DEFAULT 'active'
                               CHECK (status IN ('active','trialing','past_due','canceled','unpaid')),
    current_period_start     TIMESTAMPTZ,
    current_period_end       TIMESTAMPTZ,
    cancel_at_period_end     BOOLEAN NOT NULL DEFAULT FALSE,
    created_at               TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at               TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_subscriptions_customer
    ON subscriptions (stripe_customer_id);

CREATE TABLE IF NOT EXISTS usage_counters (
    user_id          TEXT NOT NULL,
    period_start     TIMESTAMPTZ NOT NULL,
    llm_intents      INTEGER NOT NULL DEFAULT 0,
    compiles         INTEGER NOT NULL DEFAULT 0,
    deploys          INTEGER NOT NULL DEFAULT 0,
    devices_active   INTEGER NOT NULL DEFAULT 0,
    updated_at       TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (user_id, period_start)
);

CREATE INDEX IF NOT EXISTS idx_usage_counters_period
    ON usage_counters (period_start);

-- RLS: users can see their own subscription + usage only
ALTER TABLE subscriptions   ENABLE ROW LEVEL SECURITY;
ALTER TABLE usage_counters  ENABLE ROW LEVEL SECURITY;

DROP POLICY IF EXISTS subscriptions_self ON subscriptions;
CREATE POLICY subscriptions_self ON subscriptions
    FOR SELECT USING (auth.uid()::text = user_id);

DROP POLICY IF EXISTS usage_counters_self ON usage_counters;
CREATE POLICY usage_counters_self ON usage_counters
    FOR SELECT USING (auth.uid()::text = user_id);

-- Service role bypasses RLS for webhook + backend mutations.
