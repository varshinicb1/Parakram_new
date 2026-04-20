//! Marketplace database helpers.
//!
//! All functions are thin, focused wrappers over `sqlx`.  They own no business
//! logic — that belongs in the API layer.  Every function takes a `&PgPool`
//! rather than the full `AppState` so they can be tested in isolation.

use sqlx::PgPool;
use uuid::Uuid;

use super::{CommunityDriver, CommunityDriverFull};

// ── Filters & sort ────────────────────────────────────────────────────────────

/// Controls which rows are returned by [`list_approved`].
pub struct ListFilter {
    /// Free-text search applied as `ILIKE '%…%'` on `name` and `description`.
    pub search: Option<String>,
    /// Exact match on `driver_type`.
    pub driver_type: Option<String>,
    /// Match a single bus type — the driver must support it (`= ANY(bus_types)`).
    pub bus_type: Option<String>,
    /// Match a single capability — the driver must have it (`= ANY(capabilities)`).
    pub capability: Option<String>,
    pub sort: SortOrder,
    /// Page size (max 100 enforced in the function).
    pub limit: i64,
    pub offset: i64,
}

pub enum SortOrder {
    Newest,
    Downloads,
    Stars,
}

// ── Input types ───────────────────────────────────────────────────────────────

/// Data required to insert a new community driver submission.
pub struct SubmitInput<'a> {
    pub author_id: &'a str,
    pub name: &'a str,
    pub display_name: &'a str,
    pub description: &'a str,
    pub version: &'a str,
    pub driver_type: &'a str,
    pub bus_types: &'a [String],
    pub capabilities: &'a [String],
    pub source_code: &'a str,
    pub validation_json: serde_json::Value,
}

// ── Queries ───────────────────────────────────────────────────────────────────

/// Return all approved drivers matching `filter`, ordered by `sort`.
///
/// Dynamic WHERE clauses are assembled in Rust and sent as a single query.
/// Max page size is clamped to 100.
pub async fn list_approved(
    db: &PgPool,
    filter: &ListFilter,
) -> Result<Vec<CommunityDriver>, sqlx::Error> {
    let limit = filter.limit.min(100);
    let offset = filter.offset;

    // Build the WHERE fragment dynamically.
    let mut conditions: Vec<String> = vec!["status = 'approved'".to_string()];
    let mut param_idx: i32 = 1; // PostgreSQL positional params start at $1

    // We will bind values in the same order we push conditions.
    // Collect them as boxed Any-compatible types via a helper below.
    let mut search_val: Option<String> = None;
    let mut type_val: Option<String> = None;
    let mut bus_val: Option<String> = None;
    let mut cap_val: Option<String> = None;

    if let Some(ref q) = filter.search {
        conditions.push(format!(
            "(name ILIKE ${p} OR description ILIKE ${p})",
            p = param_idx
        ));
        search_val = Some(format!("%{}%", q));
        param_idx += 1;
    }
    if filter.driver_type.is_some() {
        conditions.push(format!("driver_type = ${}", param_idx));
        type_val = filter.driver_type.clone();
        param_idx += 1;
    }
    if filter.bus_type.is_some() {
        conditions.push(format!("${} = ANY(bus_types)", param_idx));
        bus_val = filter.bus_type.clone();
        param_idx += 1;
    }
    if filter.capability.is_some() {
        conditions.push(format!("${} = ANY(capabilities)", param_idx));
        cap_val = filter.capability.clone();
        param_idx += 1;
    }

    let order = match filter.sort {
        SortOrder::Newest    => "created_at DESC",
        SortOrder::Downloads => "downloads DESC",
        SortOrder::Stars     => "(CASE WHEN stars_count = 0 THEN 0.0 ELSE stars_total::float / stars_count END) DESC",
    };

    // $N and $N+1 are limit and offset — assigned after all filter params.
    let sql = format!(
        "SELECT id, author_id, name, display_name, description, version, \
                driver_type, bus_types, capabilities, status, rejection_reason, \
                downloads, stars_total, stars_count, created_at, updated_at \
         FROM community_drivers \
         WHERE {} \
         ORDER BY {} \
         LIMIT ${} OFFSET ${}",
        conditions.join(" AND "),
        order,
        param_idx,
        param_idx + 1,
    );

    let mut q = sqlx::query_as::<_, CommunityDriver>(&sql);

    // Bind filter params in the same order they were pushed.
    if let Some(ref v) = search_val { q = q.bind(v); }
    if let Some(ref v) = type_val   { q = q.bind(v); }
    if let Some(ref v) = bus_val    { q = q.bind(v); }
    if let Some(ref v) = cap_val    { q = q.bind(v); }

    // Bind pagination last.
    q = q.bind(limit).bind(offset);

    q.fetch_all(db).await
}

/// Count total approved drivers matching the same filter, without LIMIT/OFFSET.
/// Used to return accurate pagination totals.
pub async fn count_approved(
    db: &PgPool,
    filter: &ListFilter,
) -> Result<i64, sqlx::Error> {
    let mut conditions: Vec<String> = vec!["status = 'approved'".to_string()];
    let mut param_idx: i32 = 1;
    let mut search_val: Option<String> = None;
    let mut type_val:   Option<String> = None;
    let mut bus_val:    Option<String> = None;
    let mut cap_val:    Option<String> = None;

    if let Some(ref q) = filter.search {
        conditions.push(format!("(name ILIKE ${p} OR description ILIKE ${p})", p = param_idx));
        search_val = Some(format!("%{}%", q));
        param_idx += 1;
    }
    if filter.driver_type.is_some() {
        conditions.push(format!("driver_type = ${}", param_idx));
        type_val = filter.driver_type.clone();
        param_idx += 1;
    }
    if filter.bus_type.is_some() {
        conditions.push(format!("${} = ANY(bus_types)", param_idx));
        bus_val = filter.bus_type.clone();
        param_idx += 1;
    }
    if filter.capability.is_some() {
        conditions.push(format!("${} = ANY(capabilities)", param_idx));
        cap_val = filter.capability.clone();
        let _ = param_idx; // consumed
    }

    let sql = format!(
        "SELECT COUNT(*) FROM community_drivers WHERE {}",
        conditions.join(" AND "),
    );

    let mut q = sqlx::query_scalar::<_, i64>(&sql);
    if let Some(ref v) = search_val { q = q.bind(v); }
    if let Some(ref v) = type_val   { q = q.bind(v); }
    if let Some(ref v) = bus_val    { q = q.bind(v); }
    if let Some(ref v) = cap_val    { q = q.bind(v); }

    q.fetch_one(db).await
}

/// Fetch a single driver by UUID, including source code and validation JSON.
///
/// Returns `None` when no row with that ID exists.
pub async fn get_by_id(
    db: &PgPool,
    id: Uuid,
) -> Result<Option<CommunityDriverFull>, sqlx::Error> {
    sqlx::query_as::<_, CommunityDriverFull>(
        "SELECT id, author_id, name, display_name, description, version, \
                driver_type, bus_types, capabilities, source_code, status, \
                rejection_reason, validation_json, downloads, stars_total, \
                stars_count, created_at, updated_at \
         FROM community_drivers \
         WHERE id = $1",
    )
    .bind(id)
    .fetch_optional(db)
    .await
}

/// Look up a driver by its unique `name` in the community table.
///
/// Used for name-conflict detection before a submission is accepted.
/// Official registry names are checked separately in the API layer.
pub async fn get_by_name(
    db: &PgPool,
    name: &str,
) -> Result<Option<CommunityDriver>, sqlx::Error> {
    sqlx::query_as::<_, CommunityDriver>(
        "SELECT id, author_id, name, display_name, description, version, \
                driver_type, bus_types, capabilities, status, rejection_reason, \
                downloads, stars_total, stars_count, created_at, updated_at \
         FROM community_drivers \
         WHERE name = $1 \
         LIMIT 1",
    )
    .bind(name)
    .fetch_optional(db)
    .await
}

/// Insert a new driver submission with status `'pending'`.
///
/// Returns the UUID assigned to the new row.
pub async fn submit(db: &PgPool, sub: &SubmitInput<'_>) -> Result<Uuid, sqlx::Error> {
    let row: (Uuid,) = sqlx::query_as(
        "INSERT INTO community_drivers \
             (author_id, name, display_name, description, version, \
              driver_type, bus_types, capabilities, source_code, \
              validation_json, status, downloads, stars_total, stars_count, \
              created_at, updated_at) \
         VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, \
                 'pending', 0, 0, 0, NOW(), NOW()) \
         RETURNING id",
    )
    .bind(sub.author_id)
    .bind(sub.name)
    .bind(sub.display_name)
    .bind(sub.description)
    .bind(sub.version)
    .bind(sub.driver_type)
    .bind(sub.bus_types)
    .bind(sub.capabilities)
    .bind(sub.source_code)
    .bind(&sub.validation_json)
    .fetch_one(db)
    .await?;

    Ok(row.0)
}

/// Update a driver's moderation status and optional rejection reason.
///
/// Typically called by admin handlers (`approve` / `reject`).
pub async fn update_status(
    db: &PgPool,
    id: Uuid,
    status: &str,
    reason: Option<&str>,
) -> Result<(), sqlx::Error> {
    sqlx::query(
        "UPDATE community_drivers \
         SET status = $1, rejection_reason = $2, updated_at = NOW() \
         WHERE id = $3",
    )
    .bind(status)
    .bind(reason)
    .bind(id)
    .execute(db)
    .await?;
    Ok(())
}

/// Withdraw a pending submission owned by `author_id`.
///
/// The UPDATE is constrained by both `author_id` and `status = 'pending'` so
/// that users cannot retract an already-approved driver.
///
/// Returns `true` if the row was updated (i.e. it existed and was pending).
pub async fn withdraw(
    db: &PgPool,
    id: Uuid,
    author_id: &str,
) -> Result<bool, sqlx::Error> {
    let result = sqlx::query(
        "UPDATE community_drivers \
         SET status = 'withdrawn', updated_at = NOW() \
         WHERE id = $1 AND author_id = $2 AND status = 'pending'",
    )
    .bind(id)
    .bind(author_id)
    .execute(db)
    .await?;
    Ok(result.rows_affected() > 0)
}

/// Increment the download counter by one.
pub async fn increment_downloads(db: &PgPool, id: Uuid) -> Result<(), sqlx::Error> {
    sqlx::query(
        "UPDATE community_drivers \
         SET downloads = downloads + 1, updated_at = NOW() \
         WHERE id = $1",
    )
    .bind(id)
    .execute(db)
    .await?;
    Ok(())
}

/// Insert or update a rating, then sync the aggregate columns on the driver row.
///
/// The `driver_ratings` table must have a UNIQUE constraint on `(user_id, driver_id)`.
/// After the upsert, `stars_total` and `stars_count` on `community_drivers` are
/// recalculated from the live ratings table — no stale counters.
pub async fn upsert_rating(
    db: &PgPool,
    user_id: &str,
    driver_id: Uuid,
    stars: i32,
    review: Option<&str>,
) -> Result<(), sqlx::Error> {
    // Upsert the individual rating row.
    sqlx::query(
        "INSERT INTO driver_ratings (user_id, driver_id, stars, review, created_at, updated_at) \
         VALUES ($1, $2, $3, $4, NOW(), NOW()) \
         ON CONFLICT (user_id, driver_id) \
         DO UPDATE SET stars = EXCLUDED.stars, review = EXCLUDED.review, updated_at = NOW()",
    )
    .bind(user_id)
    .bind(driver_id)
    .bind(stars)
    .bind(review)
    .execute(db)
    .await?;

    // Recompute aggregate columns from the source-of-truth ratings table.
    sqlx::query(
        "UPDATE community_drivers cd \
         SET stars_total = agg.total, \
             stars_count = agg.cnt, \
             updated_at  = NOW() \
         FROM ( \
             SELECT driver_id, \
                    COALESCE(SUM(stars), 0) AS total, \
                    COUNT(*) AS cnt \
             FROM driver_ratings \
             WHERE driver_id = $1 \
             GROUP BY driver_id \
         ) agg \
         WHERE cd.id = agg.driver_id",
    )
    .bind(driver_id)
    .execute(db)
    .await?;

    Ok(())
}

/// Record a driver install for a user.
///
/// Uses `ON CONFLICT DO NOTHING` so repeat installs are silently ignored.
///
/// Returns `true` if this is a new install (the row did not already exist).
pub async fn install(
    db: &PgPool,
    user_id: &str,
    driver_id: Uuid,
) -> Result<bool, sqlx::Error> {
    let result = sqlx::query(
        "INSERT INTO driver_installs (user_id, driver_id, installed_at) \
         VALUES ($1, $2, NOW()) \
         ON CONFLICT (user_id, driver_id) DO NOTHING",
    )
    .bind(user_id)
    .bind(driver_id)
    .execute(db)
    .await?;
    Ok(result.rows_affected() > 0)
}

/// List all submissions by a given author, newest first.
pub async fn list_my_submissions(
    db: &PgPool,
    author_id: &str,
) -> Result<Vec<CommunityDriver>, sqlx::Error> {
    sqlx::query_as::<_, CommunityDriver>(
        "SELECT id, author_id, name, display_name, description, version, \
                driver_type, bus_types, capabilities, status, rejection_reason, \
                downloads, stars_total, stars_count, created_at, updated_at \
         FROM community_drivers \
         WHERE author_id = $1 \
         ORDER BY created_at DESC",
    )
    .bind(author_id)
    .fetch_all(db)
    .await
}
