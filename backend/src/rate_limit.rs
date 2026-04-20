use axum::{
    body::Body,
    extract::ConnectInfo,
    http::{Request, StatusCode},
    middleware::Next,
    response::{IntoResponse, Json, Response},
};
use lazy_static::lazy_static;
use serde_json::json;
use std::{
    collections::HashMap,
    net::{IpAddr, SocketAddr},
    sync::Mutex,
    time::{Duration, Instant},
};

struct RateLimitEntry {
    count: u32,
    window_start: Instant,
}

struct RateLimiter {
    buckets: Mutex<HashMap<IpAddr, RateLimitEntry>>,
    max_requests: u32,
    window: Duration,
}

impl RateLimiter {
    fn new(max_requests: u32, window_secs: u64) -> Self {
        Self {
            buckets: Mutex::new(HashMap::new()),
            max_requests,
            window: Duration::from_secs(window_secs),
        }
    }

    fn check(&self, ip: IpAddr) -> bool {
        let mut map = self.buckets.lock().unwrap();
        let now = Instant::now();

        // Evict stale entries periodically (every ~1000 entries)
        if map.len() > 1000 {
            map.retain(|_, e| now.duration_since(e.window_start) < self.window * 2);
        }

        let entry = map.entry(ip).or_insert(RateLimitEntry {
            count: 0,
            window_start: now,
        });

        if now.duration_since(entry.window_start) >= self.window {
            entry.count = 0;
            entry.window_start = now;
        }

        entry.count += 1;
        entry.count <= self.max_requests
    }
}

lazy_static! {
    // Auth endpoints: 10 requests per 60 seconds per IP
    static ref AUTH_LIMITER: RateLimiter = RateLimiter::new(10, 60);
    // General API: 120 requests per 60 seconds per IP
    static ref API_LIMITER: RateLimiter = RateLimiter::new(120, 60);
    // LLM endpoints: 5 requests per 60 seconds per IP
    static ref LLM_LIMITER: RateLimiter = RateLimiter::new(5, 60);
}

fn extract_ip(req: &Request<Body>) -> IpAddr {
    // Trust X-Real-IP / X-Forwarded-For from reverse proxy
    if let Some(real_ip) = req
        .headers()
        .get("x-real-ip")
        .and_then(|v| v.to_str().ok())
        .and_then(|s| s.parse().ok())
    {
        return real_ip;
    }
    if let Some(forwarded) = req
        .headers()
        .get("x-forwarded-for")
        .and_then(|v| v.to_str().ok())
        .and_then(|s| s.split(',').next())
        .and_then(|s| s.trim().parse().ok())
    {
        return forwarded;
    }
    // Fall back to peer address
    req.extensions()
        .get::<ConnectInfo<SocketAddr>>()
        .map(|ci| ci.0.ip())
        .unwrap_or(IpAddr::from([127, 0, 0, 1]))
}

fn too_many_requests() -> Response {
    (
        StatusCode::TOO_MANY_REQUESTS,
        Json(json!({
            "error": {
                "code": "RATE_LIMITED",
                "message": "Too many requests — please slow down"
            }
        })),
    )
        .into_response()
}

/// Middleware for auth endpoints (register, login, forgot-password): 10 req/min/IP
pub async fn auth_rate_limit(req: Request<Body>, next: Next) -> Response {
    let ip = extract_ip(&req);
    if !AUTH_LIMITER.check(ip) {
        tracing::warn!("Auth rate limit exceeded for IP {}", ip);
        return too_many_requests();
    }
    next.run(req).await
}

/// Middleware for LLM endpoints: 5 req/min/IP
pub async fn llm_rate_limit(req: Request<Body>, next: Next) -> Response {
    let ip = extract_ip(&req);
    if !LLM_LIMITER.check(ip) {
        tracing::warn!("LLM rate limit exceeded for IP {}", ip);
        return too_many_requests();
    }
    next.run(req).await
}

/// General API rate limit: 120 req/min/IP
pub async fn api_rate_limit(req: Request<Body>, next: Next) -> Response {
    let ip = extract_ip(&req);
    if !API_LIMITER.check(ip) {
        tracing::warn!("API rate limit exceeded for IP {}", ip);
        return too_many_requests();
    }
    next.run(req).await
}
