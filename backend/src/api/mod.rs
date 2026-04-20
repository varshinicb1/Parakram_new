//! API module — all Axum HTTP endpoint handlers.

pub mod assets;
pub mod auth;
pub mod billing;
pub mod configurator;
pub mod devices;
pub mod fleet;
pub mod ir;
pub mod issues;
pub mod llm;
pub mod marketplace;
pub mod notifications;
pub mod ota;
pub mod project_mgmt;
pub mod projects;
pub mod ros;
pub mod system;
pub mod telemetry_ws;
pub mod templates;
pub mod users;

use axum::{Router, middleware::{self, Next}};
use axum::routing::get;
use axum::Json;
use axum::http::{Request, StatusCode};
use axum::response::IntoResponse;
use std::sync::atomic::{AtomicUsize, Ordering};

use crate::AppState;
use crate::rate_limit;

static ACTIVE_REQUESTS: AtomicUsize = AtomicUsize::new(0);

async fn rate_limit_middleware(
    req: Request<axum::body::Body>,
    next: Next,
) -> Result<impl IntoResponse, StatusCode> {
    let current = ACTIVE_REQUESTS.fetch_add(1, Ordering::SeqCst);
    if current > 10 { // Max 10 concurrent heavy requests
        ACTIVE_REQUESTS.fetch_sub(1, Ordering::SeqCst);
        return Err(StatusCode::TOO_MANY_REQUESTS);
    }
    let response = next.run(req).await;
    ACTIVE_REQUESTS.fetch_sub(1, Ordering::SeqCst);
    Ok(response)
}

/// Count every request and bucket by 2xx/4xx/5xx for /metrics.
async fn metrics_middleware(
    req: Request<axum::body::Body>,
    next: Next,
) -> axum::response::Response {
    crate::metrics::REQUESTS_TOTAL.inc();
    let resp = next.run(req).await;
    let code = resp.status().as_u16();
    match code {
        200..=299 => crate::metrics::REQUESTS_2XX.inc(),
        400..=499 => crate::metrics::REQUESTS_4XX.inc(),
        500..=599 => crate::metrics::REQUESTS_5XX.inc(),
        _ => {}
    }
    resp
}

/// Build the complete API router.
pub fn router() -> Router<AppState> {
    Router::new()
        .layer(middleware::from_fn(metrics_middleware))
        .nest("/auth", auth::router()
            .layer(middleware::from_fn(rate_limit::auth_rate_limit)))
        .nest("/billing", billing::router())
        .nest("/projects", projects::router())
        .nest("/project", project_mgmt::router())
        .nest("/ir", ir::router())
        .nest("/llm", llm::router()
            .layer(middleware::from_fn(rate_limit::llm_rate_limit)))
        .nest("/ros", ros::router())
        .nest("/devices", devices::router())
        .nest("/drivers", system::drivers_router())
        .nest("/boards", system::boards_router())
        .nest("/system", system::router())
        .nest("/assets", assets::router())
        .nest("/configure", configurator::router())
        .nest("/marketplace", marketplace::router())
        .nest("/notifications", notifications::router())
        .nest("/ota", ota::router())
        .nest("/telemetry", telemetry_ws::router())
        .nest("/fleet", fleet::router())
        .nest("/users", users::router())
        .nest("/issues", issues::router())
        .route("/templates", get(list_templates))
}

async fn list_templates() -> Json<Vec<templates::Template>> {
    Json(templates::get_all_templates())
}

