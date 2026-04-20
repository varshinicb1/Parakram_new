//! Auth — Supabase JWT validation + native register/login.
//! Supports both current ES256 (P-256) and legacy HS256 JWTs.
//! Native sign-up/sign-in use PBKDF2-HMAC-SHA256 password hashing.

use axum::{extract::State, http::StatusCode, routing::{get, post, put}, Json, Router};
use serde::{Deserialize, Serialize};
use jsonwebtoken::{decode, decode_header, encode, DecodingKey, EncodingKey, Header, Validation, Algorithm};
use chrono::Utc;
use uuid::Uuid;
use ring::{pbkdf2, rand::{self, SecureRandom}};
use crate::AppState;

static PBKDF2_ALG: pbkdf2::Algorithm = pbkdf2::PBKDF2_HMAC_SHA256;
const ITERATIONS: u32 = 100_000;
const SALT_LEN: usize = 16;
const HASH_LEN: usize = 32;

pub fn router() -> Router<AppState> {
    Router::new()
        .route("/me", get(me))
        .route("/me/llm-key", put(set_llm_key))
        .route("/register", post(register))
        .route("/login", post(login))
        .route("/verify-email", post(verify_email))
        .route("/forgot-password", post(forgot_password))
        .route("/reset-password", post(reset_password))
}

// ── Shared types ─────────────────────────────────────────────────────────────

/// Claims inside a Supabase-issued JWT (both ES256 and HS256).
#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct Claims {
    pub sub: String, // Supabase user UUID
    #[serde(default)]
    pub email: Option<String>,
    #[serde(default)]
    pub role: Option<String>,
    pub aud: Vec<String>,
    pub exp: usize,
    pub iat: usize,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
struct IssueClaims {
    pub sub: String,
    pub email: Option<String>,
    pub role: String,
    pub aud: Vec<String>,
    pub iat: usize,
    pub exp: usize,
}

#[derive(Debug, Serialize)]
pub struct ErrorBody {
    pub error: ErrorDetail,
}

#[derive(Debug, Serialize)]
pub struct ErrorDetail {
    pub code: String,
    pub message: String,
}

#[derive(Debug, Serialize)]
pub struct MeResponse {
    pub user_id: String,
    pub email: Option<String>,
    pub role: Option<String>,
}

// ── Request / response types for register & login ────────────────────────────

#[derive(Debug, Deserialize)]
struct RegisterRequest {
    username: String,
    email: Option<String>,
    password: String,
}

#[derive(Debug, Deserialize)]
struct LoginRequest {
    username: String,
    password: String,
}

#[derive(Debug, Serialize)]
struct AuthResponse {
    token: String,
    expires_at: String,
    user_id: String,
    username: String,
}

// ── Request / response types for password reset ──────────────────────────────

#[derive(Debug, Deserialize)]
struct ForgotPasswordRequest {
    username: String,
}

#[derive(Debug, Serialize)]
struct MessageResponse {
    message: String,
}

#[derive(Debug, Deserialize)]
struct ResetPasswordRequest {
    username: String,
    code: String,
    new_password: String,
}

#[derive(Debug, Deserialize)]
struct VerifyEmailRequest {
    username: String,
    code: String,
}

// ── Helper: type alias for handler errors ────────────────────────────────────

type HandlerError = (StatusCode, Json<ErrorBody>);

fn err(status: StatusCode, code: &str, message: impl Into<String>) -> HandlerError {
    (status, Json(ErrorBody {
        error: ErrorDetail {
            code: code.into(),
            message: message.into(),
        },
    }))
}

// ── JWT issuing helper ────────────────────────────────────────────────────────

fn issue_jwt(
    user_id: &str,
    email: Option<&str>,
    username: &str,
    role: &str,
    secret: &str,
) -> Result<(String, String), HandlerError> {
    let now = Utc::now();
    let iat = now.timestamp() as usize;
    let exp = (now.timestamp() + 86_400) as usize;
    let expires_at = (now + chrono::Duration::seconds(86_400))
        .to_rfc3339_opts(chrono::SecondsFormat::Secs, true);

    let claims = IssueClaims {
        sub: user_id.to_string(),
        email: email.map(|s| s.to_string()),
        role: role.to_string(),
        aud: vec!["authenticated".to_string()],
        iat,
        exp,
    };

    let _ = username; // included in log by callers, kept in signature for future use

    let token = encode(
        &Header::new(Algorithm::HS256),
        &claims,
        &EncodingKey::from_secret(secret.as_bytes()),
    )
    .map_err(|e| err(
        StatusCode::INTERNAL_SERVER_ERROR,
        "JWT_ERROR",
        format!("Failed to issue token: {}", e),
    ))?;

    Ok((token, expires_at))
}

// ── Password hashing helpers ──────────────────────────────────────────────────

fn hash_password(password: &str) -> Result<String, HandlerError> {
    let rng = rand::SystemRandom::new();
    let mut salt = [0u8; SALT_LEN];
    rng.fill(&mut salt).map_err(|_| err(
        StatusCode::INTERNAL_SERVER_ERROR,
        "CRYPTO_ERROR",
        "Failed to generate salt",
    ))?;

    let mut hash = [0u8; HASH_LEN];
    pbkdf2::derive(
        PBKDF2_ALG,
        std::num::NonZeroU32::new(ITERATIONS).expect("ITERATIONS > 0"),
        &salt,
        password.as_bytes(),
        &mut hash,
    );

    Ok(format!("{}:{}", hex::encode(salt), hex::encode(hash)))
}

fn verify_password(password: &str, stored: &str) -> Result<(), HandlerError> {
    let mut parts = stored.splitn(2, ':');
    let salt_hex = parts.next().ok_or_else(|| err(
        StatusCode::INTERNAL_SERVER_ERROR,
        "HASH_FORMAT_ERROR",
        "Stored hash format invalid",
    ))?;
    let hash_hex = parts.next().ok_or_else(|| err(
        StatusCode::INTERNAL_SERVER_ERROR,
        "HASH_FORMAT_ERROR",
        "Stored hash format invalid",
    ))?;

    let salt = hex::decode(salt_hex).map_err(|_| err(
        StatusCode::INTERNAL_SERVER_ERROR,
        "HASH_FORMAT_ERROR",
        "Could not decode salt",
    ))?;
    let expected_hash = hex::decode(hash_hex).map_err(|_| err(
        StatusCode::INTERNAL_SERVER_ERROR,
        "HASH_FORMAT_ERROR",
        "Could not decode hash",
    ))?;

    pbkdf2::verify(
        PBKDF2_ALG,
        std::num::NonZeroU32::new(ITERATIONS).expect("ITERATIONS > 0"),
        &salt,
        password.as_bytes(),
        &expected_hash,
    )
    .map_err(|_| err(StatusCode::UNAUTHORIZED, "INVALID_CREDENTIALS", "Invalid credentials"))
}

// ── Handlers ──────────────────────────────────────────────────────────────────

async fn me(
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
) -> Result<Json<MeResponse>, HandlerError> {
    let token = extract_bearer_token(&headers)?;
    let claims = validate_token(&token, &state)?;
    Ok(Json(MeResponse {
        user_id: claims.sub,
        email: claims.email,
        role: claims.role,
    }))
}

#[derive(Debug, Deserialize)]
struct SetLlmKeyRequest {
    api_key: String,
}

async fn set_llm_key(
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
    Json(body): Json<SetLlmKeyRequest>,
) -> Result<Json<MessageResponse>, HandlerError> {
    let token = extract_bearer_token(&headers)?;
    let claims = validate_token(&token, &state)?;

    let key = body.api_key.trim();
    let stored = if key.is_empty() { None } else { Some(key) };

    sqlx::query("UPDATE users SET llm_api_key = $1 WHERE user_id = $2::uuid")
        .bind(stored)
        .bind(&claims.sub)
        .execute(&state.db)
        .await
        .map_err(|e| err(StatusCode::INTERNAL_SERVER_ERROR, "DB_ERROR", e.to_string()))?;

    Ok(Json(MessageResponse {
        message: if stored.is_some() {
            "OpenRouter API key saved".to_string()
        } else {
            "OpenRouter API key removed".to_string()
        },
    }))
}

async fn register(
    State(state): State<AppState>,
    Json(body): Json<RegisterRequest>,
) -> Result<(StatusCode, Json<AuthResponse>), HandlerError> {
    // 1. Validate username and password
    let username = body.username.trim();
    let password = body.password.as_str();

    if username.len() < 3 || username.len() > 32 {
        return Err(err(
            StatusCode::UNPROCESSABLE_ENTITY,
            "VALIDATION_ERROR",
            "Username must be between 3 and 32 characters",
        ));
    }
    if !username.chars().all(|c| c.is_alphanumeric() || c == '_') {
        return Err(err(
            StatusCode::UNPROCESSABLE_ENTITY,
            "VALIDATION_ERROR",
            "Username may only contain letters, digits, and underscores",
        ));
    }
    if password.len() < 8 {
        return Err(err(
            StatusCode::UNPROCESSABLE_ENTITY,
            "VALIDATION_ERROR",
            "Password must be at least 8 characters",
        ));
    }

    // 2. Check username not already taken
    let existing: Option<(String,)> = sqlx::query_as(
        "SELECT user_id::text FROM users WHERE username = $1",
    )
    .bind(username)
    .fetch_optional(&state.db)
    .await
    .map_err(|e| err(StatusCode::INTERNAL_SERVER_ERROR, "DB_ERROR", e.to_string()))?;

    if existing.is_some() {
        return Err(err(
            StatusCode::CONFLICT,
            "USERNAME_TAKEN",
            "Username is already taken",
        ));
    }

    // 3. Hash password
    let password_hash = hash_password(password)?;

    // 4. Insert user
    let new_id = Uuid::new_v4();
    let email_ref = body.email.as_deref();

    sqlx::query(
        "INSERT INTO users (user_id, username, email, password_hash, created_at)
         VALUES ($1, $2, $3, $4, NOW())",
    )
    .bind(new_id)
    .bind(username)
    .bind(email_ref)
    .bind(&password_hash)
    .execute(&state.db)
    .await
    .map_err(|e| err(StatusCode::INTERNAL_SERVER_ERROR, "DB_ERROR", e.to_string()))?;

    // 5. Issue JWT
    let user_id_str = new_id.to_string();
    let (token, expires_at) = issue_jwt(&user_id_str, email_ref, username, "user", &state.jwt_secret)?;

    tracing::info!(username = %username, user_id = %user_id_str, "User registered");

    // If email provided: send verification code; otherwise send welcome email
    if let Some(email) = &body.email {
        let rng = rand::SystemRandom::new();
        let mut raw = [0u8; 3];
        let code = if rng.fill(&mut raw).is_ok() {
            let n = (raw[0] as u32) << 16 | (raw[1] as u32) << 8 | raw[2] as u32;
            format!("{:06}", n % 900_000 + 100_000)
        } else {
            "000000".to_string()
        };

        let _ = sqlx::query(
            "INSERT INTO email_verification_tokens (token, user_id, expires_at)
             VALUES ($1, $2::uuid, NOW() + INTERVAL '15 minutes')",
        )
        .bind(&code)
        .bind(&user_id_str)
        .execute(&state.db)
        .await;

        let svc = state.email_svc.clone();
        let to = email.clone();
        let uname = username.to_string();
        tokio::spawn(async move {
            if let Err(e) = svc.send_verification(&to, &uname, &code).await {
                tracing::warn!(to = %to, "Verification email failed: {}", e);
            }
        });
    }

    Ok((StatusCode::CREATED, Json(AuthResponse {
        token,
        expires_at,
        user_id: user_id_str,
        username: username.to_string(),
    })))
}

async fn login(
    State(state): State<AppState>,
    Json(body): Json<LoginRequest>,
) -> Result<Json<AuthResponse>, HandlerError> {
    // 1. Fetch user by username (include email, email_verified, role for checks below)
    let row: Option<(String, String, String, Option<String>, bool, String)> = sqlx::query_as(
        "SELECT user_id::text, username, password_hash, email, email_verified, role
         FROM users WHERE username = $1",
    )
    .bind(&body.username)
    .fetch_optional(&state.db)
    .await
    .map_err(|e| err(StatusCode::INTERNAL_SERVER_ERROR, "DB_ERROR", e.to_string()))?;

    let (user_id_str, stored_username, stored_hash, email, email_verified, role) =
        row.ok_or_else(|| {
            err(StatusCode::UNAUTHORIZED, "INVALID_CREDENTIALS", "Invalid credentials")
        })?;

    // 2. Verify password
    verify_password(&body.password, &stored_hash)?;

    // 3. Block login if email is on file but not yet verified
    if email.is_some() && !email_verified {
        return Err(err(
            StatusCode::FORBIDDEN,
            "EMAIL_NOT_VERIFIED",
            "Please verify your email address before logging in",
        ));
    }

    // 4. Update last_login_at
    sqlx::query("UPDATE users SET last_login_at = NOW() WHERE user_id = $1::uuid")
        .bind(&user_id_str)
        .execute(&state.db)
        .await
        .map_err(|e| err(StatusCode::INTERNAL_SERVER_ERROR, "DB_ERROR", e.to_string()))?;

    // 5. Issue JWT with the user's actual role embedded
    let email_ref = email.as_deref();
    let (token, expires_at) = issue_jwt(&user_id_str, email_ref, &stored_username, &role, &state.jwt_secret)?;

    tracing::info!(username = %stored_username, user_id = %user_id_str, "User logged in");

    Ok(Json(AuthResponse {
        token,
        expires_at,
        user_id: user_id_str,
        username: stored_username,
    }))
}

// ── POST /api/auth/verify-email ──────────────────────────────────────────────

async fn verify_email(
    State(state): State<AppState>,
    Json(body): Json<VerifyEmailRequest>,
) -> Result<Json<MessageResponse>, HandlerError> {
    let username = body.username.trim();
    let code = body.code.trim();

    // 1. Find user by username
    let row: Option<(String,)> = sqlx::query_as(
        "SELECT user_id::text FROM users WHERE username = $1",
    )
    .bind(username)
    .fetch_optional(&state.db)
    .await
    .map_err(|e| err(StatusCode::INTERNAL_SERVER_ERROR, "DB_ERROR", e.to_string()))?;

    let user_id_str = row
        .ok_or_else(|| err(StatusCode::BAD_REQUEST, "INVALID_CODE", "Invalid verification code"))?
        .0;

    // 2. Validate the token
    let token_row: Option<(String,)> = sqlx::query_as(
        "SELECT token FROM email_verification_tokens
         WHERE token = $1 AND user_id = $2::uuid AND used = FALSE AND expires_at > NOW()",
    )
    .bind(code)
    .bind(&user_id_str)
    .fetch_optional(&state.db)
    .await
    .map_err(|e| err(StatusCode::INTERNAL_SERVER_ERROR, "DB_ERROR", e.to_string()))?;

    if token_row.is_none() {
        return Err(err(
            StatusCode::BAD_REQUEST,
            "INVALID_CODE",
            "Invalid or expired verification code",
        ));
    }

    // 3. Mark token used and set email_verified = true
    sqlx::query("UPDATE email_verification_tokens SET used = TRUE WHERE token = $1")
        .bind(code)
        .execute(&state.db)
        .await
        .map_err(|e| err(StatusCode::INTERNAL_SERVER_ERROR, "DB_ERROR", e.to_string()))?;

    sqlx::query("UPDATE users SET email_verified = TRUE WHERE user_id = $1::uuid")
        .bind(&user_id_str)
        .execute(&state.db)
        .await
        .map_err(|e| err(StatusCode::INTERNAL_SERVER_ERROR, "DB_ERROR", e.to_string()))?;

    tracing::info!(username = %username, user_id = %user_id_str, "Email verified");

    Ok(Json(MessageResponse {
        message: "Email verified successfully".to_string(),
    }))
}

// ── POST /api/auth/forgot-password ───────────────────────────────────────────

async fn forgot_password(
    State(state): State<AppState>,
    Json(body): Json<ForgotPasswordRequest>,
) -> Result<Json<MessageResponse>, HandlerError> {
    let username = body.username.trim();

    // Always return the same response whether the user exists or not, to avoid
    // leaking whether a username is registered.
    let ok = Json(MessageResponse {
        message: "If that username exists, a reset code has been sent".to_string(),
    });

    // 1. Look up user by username
    let row: Option<(String,)> = sqlx::query_as(
        "SELECT user_id::text FROM users WHERE username = $1",
    )
    .bind(username)
    .fetch_optional(&state.db)
    .await
    .map_err(|e| err(StatusCode::INTERNAL_SERVER_ERROR, "DB_ERROR", e.to_string()))?;

    let user_id_str = match row {
        Some((id,)) => id,
        None => {
            // User not found — return the generic message without generating a code.
            return Ok(ok);
        }
    };

    // 2. Generate a cryptographically random 6-digit reset code.
    //    Strategy: fill one byte with ring SecureRandom, then map it to
    //    100_000..=999_999 via (byte as u32) % 900_000 + 100_000.
    //    A single byte only gives 256 values; use 3 bytes to spread entropy:
    //    combine them into a u32 and take mod 900_000.
    let rng = rand::SystemRandom::new();
    let mut raw = [0u8; 3];
    rng.fill(&mut raw).map_err(|_| err(
        StatusCode::INTERNAL_SERVER_ERROR,
        "CRYPTO_ERROR",
        "Failed to generate reset code",
    ))?;
    let n = (raw[0] as u32) << 16 | (raw[1] as u32) << 8 | raw[2] as u32;
    let code = format!("{:06}", n % 900_000 + 100_000);

    // 3. Persist the token (expire any previous unused tokens for this user first).
    sqlx::query(
        "UPDATE password_reset_tokens SET used = TRUE
         WHERE user_id = $1::uuid AND used = FALSE",
    )
    .bind(&user_id_str)
    .execute(&state.db)
    .await
    .map_err(|e| err(StatusCode::INTERNAL_SERVER_ERROR, "DB_ERROR", e.to_string()))?;

    sqlx::query(
        "INSERT INTO password_reset_tokens (token, user_id, expires_at, used)
         VALUES ($1, $2::uuid, NOW() + INTERVAL '15 minutes', FALSE)",
    )
    .bind(&code)
    .bind(&user_id_str)
    .execute(&state.db)
    .await
    .map_err(|e| err(StatusCode::INTERNAL_SERVER_ERROR, "DB_ERROR", e.to_string()))?;

    // 4. Send email if user has one; fall back to log in dev mode.
    let user_email: Option<(Option<String>,)> = sqlx::query_as(
        "SELECT email FROM users WHERE user_id = $1::uuid",
    )
    .bind(&user_id_str)
    .fetch_optional(&state.db)
    .await
    .ok()
    .flatten();

    if let Some((Some(email),)) = user_email {
        if let Err(e) = state.email_svc.send_password_reset(&email, username, &code).await {
            tracing::warn!("Email send failed for {}: {}", username, e);
        }
    } else {
        tracing::info!("Reset code for {} (no email on file): {}", username, code);
    }

    Ok(ok)
}

// ── POST /api/auth/reset-password ────────────────────────────────────────────

async fn reset_password(
    State(state): State<AppState>,
    Json(body): Json<ResetPasswordRequest>,
) -> Result<Json<MessageResponse>, HandlerError> {
    let username = body.username.trim();
    let code = body.code.trim();

    // 1. Validate new password length.
    if body.new_password.len() < 8 {
        return Err(err(
            StatusCode::BAD_REQUEST,
            "VALIDATION_ERROR",
            "New password must be at least 8 characters",
        ));
    }

    // 2. Look up user by username.
    let row: Option<(String,)> = sqlx::query_as(
        "SELECT user_id::text FROM users WHERE username = $1",
    )
    .bind(username)
    .fetch_optional(&state.db)
    .await
    .map_err(|e| err(StatusCode::INTERNAL_SERVER_ERROR, "DB_ERROR", e.to_string()))?;

    let user_id_str = row.ok_or_else(|| {
        err(StatusCode::BAD_REQUEST, "INVALID_CODE", "Invalid or expired reset code")
    })?.0;

    // 3. Validate the reset token.
    let token_row: Option<(String,)> = sqlx::query_as(
        "SELECT token FROM password_reset_tokens
         WHERE token = $1
           AND user_id = $2::uuid
           AND used = FALSE
           AND expires_at > NOW()",
    )
    .bind(code)
    .bind(&user_id_str)
    .fetch_optional(&state.db)
    .await
    .map_err(|e| err(StatusCode::INTERNAL_SERVER_ERROR, "DB_ERROR", e.to_string()))?;

    if token_row.is_none() {
        return Err(err(
            StatusCode::BAD_REQUEST,
            "INVALID_CODE",
            "Invalid or expired reset code",
        ));
    }

    // 4. Hash the new password and update the user record.
    let new_hash = hash_password(&body.new_password)?;

    sqlx::query(
        "UPDATE users SET password_hash = $1 WHERE user_id = $2::uuid",
    )
    .bind(&new_hash)
    .bind(&user_id_str)
    .execute(&state.db)
    .await
    .map_err(|e| err(StatusCode::INTERNAL_SERVER_ERROR, "DB_ERROR", e.to_string()))?;

    // 5. Mark the token as used.
    sqlx::query(
        "UPDATE password_reset_tokens SET used = TRUE WHERE token = $1",
    )
    .bind(code)
    .execute(&state.db)
    .await
    .map_err(|e| err(StatusCode::INTERNAL_SERVER_ERROR, "DB_ERROR", e.to_string()))?;

    tracing::info!(username = %username, user_id = %user_id_str, "Password reset successfully");

    Ok(Json(MessageResponse {
        message: "Password reset successfully".to_string(),
    }))
}

// ── Token validation (used by other modules) ─────────────────────────────────

/// Extract the raw Bearer token from the Authorization header.
pub fn extract_bearer_token(
    headers: &axum::http::HeaderMap,
) -> Result<String, HandlerError> {
    let auth = headers
        .get("Authorization")
        .and_then(|v| v.to_str().ok())
        .ok_or_else(|| err(
            StatusCode::UNAUTHORIZED,
            "MISSING_TOKEN",
            "Authorization header required",
        ))?;

    if !auth.starts_with("Bearer ") {
        return Err(err(
            StatusCode::UNAUTHORIZED,
            "INVALID_TOKEN",
            "Bearer token required",
        ));
    }

    Ok(auth[7..].to_string())
}

/// Validate a Supabase JWT — supports both ES256 (current) and HS256 (legacy).
///
/// ES256 path: reads JWKS cache from AppState, verifies with P-256 public key.
/// HS256 path: falls back to legacy shared secret in AppState.
pub fn validate_token(
    token: &str,
    state: &AppState,
) -> Result<Claims, HandlerError> {
    let header = decode_header(token).map_err(|e| err(
        StatusCode::UNAUTHORIZED,
        "INVALID_TOKEN",
        e.to_string(),
    ))?;

    match header.alg {
        Algorithm::ES256 => verify_es256(token, &header, state),
        Algorithm::HS256 | Algorithm::HS384 | Algorithm::HS512 => {
            verify_hs256(token, &state.jwt_secret)
        }
        alg => Err(err(
            StatusCode::UNAUTHORIZED,
            "UNSUPPORTED_ALG",
            format!("JWT algorithm {:?} not supported", alg),
        )),
    }
}

fn verify_es256(
    token: &str,
    header: &jsonwebtoken::Header,
    state: &AppState,
) -> Result<Claims, HandlerError> {
    let kid = header.kid.as_deref().unwrap_or("");

    let cache = state.jwks_cache.read().map_err(|_| err(
        StatusCode::INTERNAL_SERVER_ERROR,
        "LOCK_ERROR",
        "Internal key store error",
    ))?;
    let jwks = cache.as_ref().ok_or_else(|| err(
        StatusCode::SERVICE_UNAVAILABLE,
        "JWKS_UNAVAILABLE",
        "JWT signing keys not yet fetched — retry in a moment",
    ))?;

    let jwk = jwks.find(kid).ok_or_else(|| err(
        StatusCode::UNAUTHORIZED,
        "UNKNOWN_KEY",
        format!("No signing key found for kid={}", kid),
    ))?;

    let key = DecodingKey::from_jwk(jwk).map_err(|e| err(
        StatusCode::INTERNAL_SERVER_ERROR,
        "KEY_ERROR",
        e.to_string(),
    ))?;

    let mut validation = Validation::new(Algorithm::ES256);
    validation.set_audience(&["authenticated"]);

    let data = decode::<Claims>(token, &key, &validation).map_err(|e| err(
        StatusCode::UNAUTHORIZED,
        "INVALID_TOKEN",
        e.to_string(),
    ))?;

    Ok(data.claims)
}

fn verify_hs256(token: &str, secret: &str) -> Result<Claims, HandlerError> {
    let mut validation = Validation::new(Algorithm::HS256);
    validation.set_audience(&["authenticated"]);

    let data = decode::<Claims>(
        token,
        &DecodingKey::from_secret(secret.as_bytes()),
        &validation,
    )
    .map_err(|e| err(
        StatusCode::UNAUTHORIZED,
        "INVALID_TOKEN",
        e.to_string(),
    ))?;

    Ok(data.claims)
}
