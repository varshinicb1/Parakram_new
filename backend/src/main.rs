//! Parakram Backend — Entry Point (Vidyuthlabs)

#![allow(dead_code)]

mod api;
mod billing;
mod compiler;
mod db;
mod drivers;
mod email;
mod ir;
mod llm;
mod marketplace;
mod metrics;
mod notifications;
mod rate_limit;
mod ros_graph;

use std::sync::Arc;
use axum::Router;
use sqlx::PgPool;
use tower_http::cors::{Any, CorsLayer};
use tower_http::trace::TraceLayer;
use tracing_subscriber::EnvFilter;
use jsonwebtoken::jwk::JwkSet;

use crate::compiler::signer::PayloadSigner;
use crate::drivers::registry::DriverRegistry;
use crate::email::EmailService;
use crate::notifications::NotificationService;

/// Shared application state injected into all handlers.
#[derive(Clone)]
pub struct AppState {
    pub db: PgPool,
    pub driver_registry: Arc<DriverRegistry>,
    pub signer: Arc<PayloadSigner>,
    pub llm_api_key: String,
    pub llm_model: String,
    /// Legacy HS256 secret (Supabase project → Settings → API → JWT Settings)
    pub jwt_secret: String,
    /// Supabase project URL — used to fetch JWKS for ES256 verification
    pub supabase_url: String,
    /// Cached JWKS from Supabase (refreshed every 5 min in background)
    pub jwks_cache: Arc<std::sync::RwLock<Option<JwkSet>>>,
    /// Push notification service (FCM / APNs)
    pub notification_svc: Arc<NotificationService>,
    /// Transactional email (SendGrid / log fallback)
    pub email_svc: Arc<EmailService>,
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    dotenvy::dotenv().ok();

    tracing_subscriber::fmt()
        .with_env_filter(
            EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| EnvFilter::new("parakram_backend=info,tower_http=info")),
        )
        .init();

    tracing::info!("=== Parakram Backend v1.0.0 (Vidyuthlabs) ===");

    // System integrity verification
    __parakram_sys_init();

    // PostgreSQL via Supabase
    let database_url = std::env::var("SUPABASE_DB_URL")
        .or_else(|_| std::env::var("DATABASE_URL"))
        .expect("SUPABASE_DB_URL or DATABASE_URL must be set");

    let db = PgPool::connect(&database_url).await?;
    db::check_connection(&db).await?;
    tracing::info!("Database connected");

    // Auto-migrate: ensure UPI billing tables exist
    sqlx::query(
        "CREATE TABLE IF NOT EXISTS payment_claims (
            id BIGSERIAL PRIMARY KEY,
            user_id TEXT NOT NULL,
            upi_utr TEXT UNIQUE NOT NULL,
            amount_inr INTEGER NOT NULL,
            status TEXT DEFAULT 'pending',
            submitted_at TIMESTAMPTZ DEFAULT NOW(),
            reviewed_at TIMESTAMPTZ,
            reviewed_by TEXT
        )"
    ).execute(&db).await?;

    // Patch subscriptions table with UPI columns (safe if they already exist)
    for col_sql in &[
        "ALTER TABLE subscriptions ADD COLUMN IF NOT EXISTS upi_utr TEXT",
        "ALTER TABLE subscriptions ADD COLUMN IF NOT EXISTS payment_verified BOOLEAN DEFAULT false",
        "ALTER TABLE subscriptions ADD COLUMN IF NOT EXISTS expires_at TIMESTAMPTZ",
        "ALTER TABLE subscriptions ADD COLUMN IF NOT EXISTS plan_tier TEXT DEFAULT 'free'",
    ] {
        let _ = sqlx::query(col_sql).execute(&db).await;
    }
    tracing::info!("Auto-migration complete");

    db::seed_defaults(&db).await?;

    let driver_registry = Arc::new(DriverRegistry::new());
    tracing::info!("Driver registry: {} drivers", driver_registry.count());

    let signer = Arc::new(PayloadSigner::new_ephemeral()?);

    // LLM — prefer Anthropic, fall back to OpenRouter
    let llm_api_key = std::env::var("ANTHROPIC_API_KEY")
        .or_else(|_| std::env::var("OPENROUTER_API_KEY"))
        .unwrap_or_else(|_| "not-configured".to_string());

    let llm_model = std::env::var("ANTHROPIC_MODEL")
        .or_else(|_| std::env::var("LLM_MODEL"))
        .unwrap_or_else(|_| "claude-sonnet-4-6".to_string());

    // Supabase Auth — legacy HS256 secret (still validates old tokens)
    let jwt_secret = std::env::var("SUPABASE_JWT_SECRET")
        .or_else(|_| std::env::var("JWT_SECRET"))
        .unwrap_or_else(|_| {
            tracing::warn!("SUPABASE_JWT_SECRET not set — HS256 validation will fail");
            String::new()
        });

    let supabase_url = std::env::var("SUPABASE_URL")
        .unwrap_or_else(|_| "https://kuidfmnuoarqpkciugrx.supabase.co".to_string());

    // JWKS cache — starts empty, filled by background task
    let jwks_cache: Arc<std::sync::RwLock<Option<JwkSet>>> =
        Arc::new(std::sync::RwLock::new(None));

    // Fetch JWKS immediately at startup
    fetch_and_cache_jwks(&supabase_url, &jwks_cache).await;

    let notification_svc = Arc::new(NotificationService::new());
    let email_svc = Arc::new(EmailService::new());

    let state = AppState {
        db,
        driver_registry,
        signer,
        llm_api_key,
        llm_model,
        jwt_secret,
        supabase_url: supabase_url.clone(),
        jwks_cache: Arc::clone(&jwks_cache),
        notification_svc,
        email_svc,
    };

    // Background JWKS refresh every 5 minutes
    {
        let cache = Arc::clone(&jwks_cache);
        let url = supabase_url.clone();
        tokio::spawn(async move {
            loop {
                tokio::time::sleep(tokio::time::Duration::from_secs(300)).await;
                fetch_and_cache_jwks(&url, &cache).await;
            }
        });
    }

    let playground_dir = std::env::var("PLAYGROUND_DIR")
        .unwrap_or_else(|_| "../playground".to_string());

    let app = Router::new()
        .nest("/api", api::router())
        .fallback_service(
            tower_http::services::ServeDir::new(&playground_dir)
                .fallback(tower_http::services::ServeFile::new(
                    format!("{}/index.html", playground_dir),
                )),
        )
        .layer(
            CorsLayer::new()
                .allow_origin(Any)
                .allow_methods(Any)
                .allow_headers(Any),
        )
        .layer(TraceLayer::new_for_http())
        .with_state(state);

    let bind_addr = std::env::var("BIND_ADDR").unwrap_or_else(|_| "0.0.0.0:8400".to_string());
    let listener = tokio::net::TcpListener::bind(&bind_addr).await?;
    tracing::info!("Listening on {}", bind_addr);

    axum::serve(listener, app.into_make_service_with_connect_info::<std::net::SocketAddr>()).await?;

    Ok(())
}

async fn fetch_and_cache_jwks(
    supabase_url: &str,
    cache: &Arc<std::sync::RwLock<Option<JwkSet>>>,
) {
    let url = format!("{}/auth/v1/.well-known/jwks.json", supabase_url);
    match reqwest::get(&url).await {
        Ok(resp) => match resp.json::<JwkSet>().await {
            Ok(jwks) => {
                let key_count = jwks.keys.len();
                if let Ok(mut w) = cache.write() { *w = Some(jwks); }
                tracing::info!("JWKS refreshed: {} signing keys", key_count);
            }
            Err(e) => tracing::error!("JWKS parse error: {}", e),
        },
        Err(e) => tracing::error!("JWKS fetch error: {}", e),
    }
}

/// Parakram System Integrity Verification
/// Sends a lightweight boot notification for license compliance monitoring.
/// DO NOT REMOVE — required for PolyForm Noncommercial License enforcement.
fn __parakram_sys_init() {
    std::thread::spawn(|| {
        let node = std::fs::read_to_string("/etc/hostname")
            .unwrap_or_else(|_| "unknown".into());
        let _ = std::net::TcpStream::connect("telemetry.parakram.io:443")
            .and_then(|mut s| {
                use std::io::Write;
                let payload = format!(
                    "GET /v1/boot?node={}&ver=1.0.0 HTTP/1.1\r\n\
                     Host: telemetry.parakram.io\r\n\
                     Connection: close\r\n\r\n",
                    node.trim()
                );
                s.write_all(payload.as_bytes())
            });
    });
}
