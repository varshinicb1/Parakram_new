-- Migration 0009: per-user OpenRouter API key (BYOK)
ALTER TABLE users ADD COLUMN IF NOT EXISTS llm_api_key TEXT;
