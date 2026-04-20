//! Driver Marketplace API — community-contributed driver submission, review, and discovery.
//!
//! All routes that modify state or read source code require a valid Supabase JWT.
//! Public listing/detail endpoints are unauthenticated.
//!
//! Route table (mounted under `/api/marketplace`):
//! ```
//! GET    /                    list_approved      (public)
//! GET    /:id                 get_driver         (public, increments downloads)
//! GET    /:id/source          get_source         (auth, hobby+ plan)
//! POST   /submit              submit_driver      (auth, hobby+ plan)
//! POST   /:id/rate            rate_driver        (auth)
//! POST   /:id/install         install_driver     (auth)
//! GET    /my/submissions      my_submissions     (auth)
//! DELETE /my/submissions/:id  withdraw_submission(auth)
//! POST   /admin/approve/:id   admin_approve      (auth, role=admin)
//! POST   /admin/reject/:id    admin_reject       (auth, role=admin)
//! ```

use axum::{
    extract::{Path, Query, State},
    http::{HeaderMap, StatusCode},
    routing::{delete, get, post},
    Json, Router,
};
use serde::{Deserialize, Serialize};
use uuid::Uuid;

use crate::api::auth::{extract_bearer_token, validate_token, Claims, ErrorBody, ErrorDetail};
use crate::billing::quota::get_plan;
use crate::billing::plans::PlanTier;
use crate::marketplace::{self, db as mdb, validator};
use crate::AppState;

// ── Router ────────────────────────────────────────────────────────────────────

pub fn router() -> Router<AppState> {
    Router::new()
        // Public
        .route("/",                     get(list_approved))
        .route("/:id",                  get(get_driver))
        .route("/:id/source",           get(get_source))
        // Authenticated — user actions
        .route("/submit",               post(submit_driver))
        .route("/:id/rate",             post(rate_driver))
        .route("/:id/install",          post(install_driver))
        .route("/my/submissions",       get(my_submissions))
        .route("/my/submissions/:id",   delete(withdraw_submission))
        // Authenticated — admin actions
        .route("/admin/approve/:id",    post(admin_approve))
        .route("/admin/reject/:id",     post(admin_reject))
}

// ── Request / response types ──────────────────────────────────────────────────

#[derive(Debug, Deserialize)]
struct ListQuery {
    search:     Option<String>,
    #[serde(rename = "type")]
    driver_type: Option<String>,
    bus:        Option<String>,
    capability: Option<String>,
    sort:       Option<String>,  // "newest" | "downloads" | "stars"
    page:       Option<i64>,
    limit:      Option<i64>,
}

#[derive(Debug, Serialize)]
struct ListResponse {
    drivers: Vec<DriverItem>,
    total:   i64,
    page:    i64,
    limit:   i64,
}

/// Serialised form of a single driver in list/detail responses.
/// Never includes `source_code`.
#[derive(Debug, Serialize)]
struct DriverItem {
    id:               Uuid,
    author_id:        String,
    name:             String,
    display_name:     String,
    description:      String,
    version:          String,
    driver_type:      String,
    bus_types:        Vec<String>,
    capabilities:     Vec<String>,
    status:           String,
    rejection_reason: Option<String>,
    downloads:        i32,
    stars_avg:        f32,
    stars_count:      i32,
    created_at:       chrono::DateTime<chrono::Utc>,
    updated_at:       chrono::DateTime<chrono::Utc>,
    validation_json:  Option<serde_json::Value>,
}

#[derive(Debug, Serialize)]
struct SourceResponse {
    source_code: String,
}

#[derive(Debug, Deserialize)]
struct SubmitBody {
    name:         String,
    display_name: String,
    description:  String,
    version:      String,
    driver_type:  String,
    source_code:  String,
}

#[derive(Debug, Serialize)]
struct SubmitResponse {
    id:      Uuid,
    name:    String,
    status:  &'static str,
    message: &'static str,
}

#[derive(Debug, Deserialize)]
struct RateBody {
    stars:  i32,
    review: Option<String>,
}

#[derive(Debug, Deserialize)]
struct RejectBody {
    reason: String,
}

// ── Helpers ───────────────────────────────────────────────────────────────────

/// Extract and validate the Bearer token from request headers.
fn auth(
    state: &AppState,
    headers: &HeaderMap,
) -> Result<Claims, (StatusCode, Json<ErrorBody>)> {
    let token = extract_bearer_token(headers)?;
    validate_token(&token, state)
}

/// Enforce that the caller holds the `admin` role.
fn require_admin(claims: &Claims) -> Result<(), (StatusCode, Json<ErrorBody>)> {
    if claims.role.as_deref() == Some("admin") {
        Ok(())
    } else {
        Err((
            StatusCode::FORBIDDEN,
            Json(ErrorBody {
                error: ErrorDetail {
                    code:    "FORBIDDEN".into(),
                    message: "admin role required".into(),
                },
            }),
        ))
    }
}

/// Map a `sqlx::Error` into a 500 response.
fn db_err(e: sqlx::Error) -> (StatusCode, Json<ErrorBody>) {
    tracing::error!("marketplace db error: {}", e);
    (
        StatusCode::INTERNAL_SERVER_ERROR,
        Json(ErrorBody {
            error: ErrorDetail {
                code:    "DB_ERROR".into(),
                message: e.to_string(),
            },
        }),
    )
}

/// Return 404 with a consistent body.
fn not_found(msg: &str) -> (StatusCode, Json<ErrorBody>) {
    (
        StatusCode::NOT_FOUND,
        Json(ErrorBody {
            error: ErrorDetail {
                code:    "NOT_FOUND".into(),
                message: msg.into(),
            },
        }),
    )
}

/// Convert a `CommunityDriver` (no source) into the API response shape.
fn driver_item_from(d: &marketplace::CommunityDriver) -> DriverItem {
    DriverItem {
        id:               d.id,
        author_id:        d.author_id.clone(),
        name:             d.name.clone(),
        display_name:     d.display_name.clone(),
        description:      d.description.clone(),
        version:          d.version.clone(),
        driver_type:      d.driver_type.clone(),
        bus_types:        d.bus_types.clone(),
        capabilities:     d.capabilities.clone(),
        status:           d.status.clone(),
        rejection_reason: d.rejection_reason.clone(),
        downloads:        d.downloads,
        stars_avg:        marketplace::avg_stars(d.stars_total, d.stars_count),
        stars_count:      d.stars_count,
        created_at:       d.created_at,
        updated_at:       d.updated_at,
        validation_json:  None,
    }
}

/// Convert a `CommunityDriverFull` into the API response shape (still no source).
fn driver_item_from_full(d: &marketplace::CommunityDriverFull) -> DriverItem {
    DriverItem {
        id:               d.id,
        author_id:        d.author_id.clone(),
        name:             d.name.clone(),
        display_name:     d.display_name.clone(),
        description:      d.description.clone(),
        version:          d.version.clone(),
        driver_type:      d.driver_type.clone(),
        bus_types:        d.bus_types.clone(),
        capabilities:     d.capabilities.clone(),
        status:           d.status.clone(),
        rejection_reason: d.rejection_reason.clone(),
        downloads:        d.downloads,
        stars_avg:        marketplace::avg_stars(d.stars_total, d.stars_count),
        stars_count:      d.stars_count,
        created_at:       d.created_at,
        updated_at:       d.updated_at,
        validation_json:  d.validation_json.clone(),
    }
}

/// Enforce that the caller is on Maker tier; 402 otherwise.
async fn require_maker_plus(
    db: &sqlx::PgPool,
    user_id: &str,
) -> Result<(), (StatusCode, Json<ErrorBody>)> {
    let tier = get_plan(db, user_id).await.map_err(db_err)?;
    if tier == PlanTier::Free {
        return Err((
            StatusCode::PAYMENT_REQUIRED,
            Json(ErrorBody {
                error: ErrorDetail {
                    code:    "PLAN_REQUIRED".into(),
                    message: "upgrade to Maker ($1.50/mo) to submit community drivers".into(),
                },
            }),
        ));
    }
    Ok(())
}

// ── Handlers ──────────────────────────────────────────────────────────────────

/// `GET /` — browse approved community drivers.
///
/// Query params: `?search=&type=&bus=&capability=&sort=newest|downloads|stars&page=1&limit=20`
async fn list_approved(
    State(state): State<AppState>,
    Query(q): Query<ListQuery>,
) -> Result<Json<ListResponse>, (StatusCode, Json<ErrorBody>)> {
    let page  = q.page.unwrap_or(1).max(1);
    let limit = q.limit.unwrap_or(20).clamp(1, 100);

    let sort = match q.sort.as_deref() {
        Some("downloads") => mdb::SortOrder::Downloads,
        Some("stars")     => mdb::SortOrder::Stars,
        _                 => mdb::SortOrder::Newest,
    };

    let filter = mdb::ListFilter {
        search:      q.search,
        driver_type: q.driver_type,
        bus_type:    q.bus,
        capability:  q.capability,
        sort,
        limit,
        offset: (page - 1) * limit,
    };

    let (rows, total) = tokio::try_join!(
        mdb::list_approved(&state.db, &filter),
        mdb::count_approved(&state.db, &filter),
    ).map_err(db_err)?;
    let drivers = rows.iter().map(driver_item_from).collect();

    Ok(Json(ListResponse { drivers, total, page, limit }))
}

/// `GET /:id` — fetch a driver's public detail and increment its download counter.
async fn get_driver(
    State(state): State<AppState>,
    Path(id): Path<Uuid>,
) -> Result<Json<DriverItem>, (StatusCode, Json<ErrorBody>)> {
    let driver = mdb::get_by_id(&state.db, id)
        .await
        .map_err(db_err)?
        .ok_or_else(|| not_found("driver not found"))?;

    // Increment asynchronously — do not block the response on the counter.
    let db = state.db.clone();
    tokio::spawn(async move {
        if let Err(e) = mdb::increment_downloads(&db, id).await {
            tracing::warn!("failed to increment downloads for {}: {}", id, e);
        }
    });

    Ok(Json(driver_item_from_full(&driver)))
}

/// `GET /:id/source` — return source code for a driver (auth + hobby+ plan required).
async fn get_source(
    State(state): State<AppState>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
) -> Result<Json<SourceResponse>, (StatusCode, Json<ErrorBody>)> {
    let claims = auth(&state, &headers)?;
    require_maker_plus(&state.db, &claims.sub).await?;

    let driver = mdb::get_by_id(&state.db, id)
        .await
        .map_err(db_err)?
        .ok_or_else(|| not_found("driver not found"))?;

    Ok(Json(SourceResponse { source_code: driver.source_code }))
}

/// `POST /submit` — submit a new community driver for moderation.
///
/// Flow:
/// 1. Authenticate + plan gate (hobby+).
/// 2. Parse body.
/// 3. Name-conflict check against official registry and community table.
/// 4. Static validation via `validator::validate`.
/// 5. Insert with `status = 'pending'`.
async fn submit_driver(
    State(state): State<AppState>,
    headers: HeaderMap,
    Json(body): Json<SubmitBody>,
) -> Result<(StatusCode, Json<SubmitResponse>), (StatusCode, Json<ErrorBody>)> {
    let claims = auth(&state, &headers)?;
    require_maker_plus(&state.db, &claims.sub).await?;

    // Collect official driver names from the in-memory registry.
    let all_specs = state.driver_registry.list_all();
    let official_names: Vec<&str> = all_specs.iter().map(|s| s.name.as_str()).collect();

    // Check for name conflict in the community table.
    if let Some(_existing) = mdb::get_by_name(&state.db, &body.name).await.map_err(db_err)? {
        return Err((
            StatusCode::CONFLICT,
            Json(ErrorBody {
                error: ErrorDetail {
                    code:    "NAME_CONFLICT".into(),
                    message: format!("a community driver named '{}' already exists", body.name),
                },
            }),
        ));
    }

    // Static analysis.
    let result = validator::validate(&body.source_code, &body.name, &official_names);

    if !result.passed {
        #[derive(Serialize)]
        struct ValidationError {
            errors:   Vec<String>,
            warnings: Vec<String>,
        }
        return Err((
            StatusCode::UNPROCESSABLE_ENTITY,
            Json(ErrorBody {
                error: ErrorDetail {
                    code:    "VALIDATION_FAILED".into(),
                    message: serde_json::to_string(&ValidationError {
                        errors:   result.errors,
                        warnings: result.warnings,
                    })
                    .unwrap_or_else(|_| "validation failed".into()),
                },
            }),
        ));
    }

    // Persist the submission.
    let validation_json = serde_json::json!({
        "bus_types":    result.detected_bus_types,
        "capabilities": result.detected_capabilities,
        "warnings":     result.warnings,
        "source_lines": result.source_lines,
    });

    let input = mdb::SubmitInput {
        author_id:       &claims.sub,
        name:            &body.name,
        display_name:    &body.display_name,
        description:     &body.description,
        version:         &body.version,
        driver_type:     &body.driver_type,
        bus_types:       &result.detected_bus_types,
        capabilities:    &result.detected_capabilities,
        source_code:     &body.source_code,
        validation_json,
    };

    let new_id = mdb::submit(&state.db, &input).await.map_err(db_err)?;

    tracing::info!(
        driver_id = %new_id,
        author    = %claims.sub,
        name      = %body.name,
        "community driver submitted for review"
    );

    Ok((
        StatusCode::CREATED,
        Json(SubmitResponse {
            id:      new_id,
            name:    body.name,
            status:  "pending",
            message: "submitted for review",
        }),
    ))
}

/// `POST /:id/rate` — submit or update a star rating (1–5) for a driver.
async fn rate_driver(
    State(state): State<AppState>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Json(body): Json<RateBody>,
) -> Result<StatusCode, (StatusCode, Json<ErrorBody>)> {
    let claims = auth(&state, &headers)?;

    if !(1..=5).contains(&body.stars) {
        return Err((
            StatusCode::UNPROCESSABLE_ENTITY,
            Json(ErrorBody {
                error: ErrorDetail {
                    code:    "INVALID_STARS".into(),
                    message: "stars must be an integer between 1 and 5".into(),
                },
            }),
        ));
    }

    // Ensure the driver exists.
    if mdb::get_by_id(&state.db, id).await.map_err(db_err)?.is_none() {
        return Err(not_found("driver not found"));
    }

    mdb::upsert_rating(
        &state.db,
        &claims.sub,
        id,
        body.stars,
        body.review.as_deref(),
    )
    .await
    .map_err(db_err)?;

    Ok(StatusCode::NO_CONTENT)
}

/// `POST /:id/install` — record a driver install for the authenticated user.
async fn install_driver(
    State(state): State<AppState>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
) -> Result<StatusCode, (StatusCode, Json<ErrorBody>)> {
    let claims = auth(&state, &headers)?;

    // Ensure the driver exists.
    if mdb::get_by_id(&state.db, id).await.map_err(db_err)?.is_none() {
        return Err(not_found("driver not found"));
    }

    let newly_installed = mdb::install(&state.db, &claims.sub, id).await.map_err(db_err)?;

    if newly_installed {
        Ok(StatusCode::CREATED)
    } else {
        Ok(StatusCode::OK)
    }
}

/// `GET /my/submissions` — list all submissions by the authenticated user.
async fn my_submissions(
    State(state): State<AppState>,
    headers: HeaderMap,
) -> Result<Json<Vec<DriverItem>>, (StatusCode, Json<ErrorBody>)> {
    let claims = auth(&state, &headers)?;

    let rows = mdb::list_my_submissions(&state.db, &claims.sub)
        .await
        .map_err(db_err)?;

    let items = rows.iter().map(driver_item_from).collect();
    Ok(Json(items))
}

/// `DELETE /my/submissions/:id` — withdraw a pending submission.
///
/// Only the author can withdraw, and only while the status is `'pending'`.
async fn withdraw_submission(
    State(state): State<AppState>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
) -> Result<StatusCode, (StatusCode, Json<ErrorBody>)> {
    let claims = auth(&state, &headers)?;

    let updated = mdb::withdraw(&state.db, id, &claims.sub)
        .await
        .map_err(db_err)?;

    if updated {
        Ok(StatusCode::NO_CONTENT)
    } else {
        Err(not_found(
            "submission not found, not owned by you, or not in pending status",
        ))
    }
}

/// `POST /admin/approve/:id` — approve a pending submission.
async fn admin_approve(
    State(state): State<AppState>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
) -> Result<StatusCode, (StatusCode, Json<ErrorBody>)> {
    let claims = auth(&state, &headers)?;
    require_admin(&claims)?;

    if mdb::get_by_id(&state.db, id).await.map_err(db_err)?.is_none() {
        return Err(not_found("driver not found"));
    }

    mdb::update_status(&state.db, id, "approved", None)
        .await
        .map_err(db_err)?;

    tracing::info!(driver_id = %id, admin = %claims.sub, "driver approved");
    Ok(StatusCode::NO_CONTENT)
}

/// `POST /admin/reject/:id` — reject a submission with a reason.
async fn admin_reject(
    State(state): State<AppState>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Json(body): Json<RejectBody>,
) -> Result<StatusCode, (StatusCode, Json<ErrorBody>)> {
    let claims = auth(&state, &headers)?;
    require_admin(&claims)?;

    if body.reason.trim().is_empty() {
        return Err((
            StatusCode::UNPROCESSABLE_ENTITY,
            Json(ErrorBody {
                error: ErrorDetail {
                    code:    "MISSING_REASON".into(),
                    message: "rejection reason must not be empty".into(),
                },
            }),
        ));
    }

    if mdb::get_by_id(&state.db, id).await.map_err(db_err)?.is_none() {
        return Err(not_found("driver not found"));
    }

    mdb::update_status(&state.db, id, "rejected", Some(body.reason.as_str()))
        .await
        .map_err(db_err)?;

    tracing::info!(driver_id = %id, admin = %claims.sub, "driver rejected");
    Ok(StatusCode::NO_CONTENT)
}
