//! Database module — PostgreSQL via Supabase.
//! Schema migrations are managed in Supabase (supabase/migrations/).
//! This module handles connection and seeding.

pub mod models;

use sqlx::PgPool;
use anyhow::Result;

/// Verify the database connection is alive.
pub async fn check_connection(db: &PgPool) -> Result<()> {
    sqlx::query("SELECT 1").execute(db).await?;
    Ok(())
}

/// Ensure the default Vidyuthlabs board SKU exists.
/// Safe to call on every startup — uses ON CONFLICT DO NOTHING.
pub async fn seed_defaults(db: &PgPool) -> Result<()> {
    let board = crate::drivers::boards::get_board_vdyt_s3_r1();
    let pin_map = serde_json::to_value(&board.available_buses)?;
    let default_devices = serde_json::to_value(&board.available_slots)?;

    sqlx::query(
        "INSERT INTO board_skus (sku, name, soc, flash_mb, psram_mb, pin_map, default_devices)
         VALUES ($1, $2, $3, $4, $5, $6, $7)
         ON CONFLICT (sku) DO NOTHING"
    )
    .bind(&board.sku)
    .bind(&board.name)
    .bind(&board.soc)
    .bind(board.flash_mb as i32)
    .bind(board.psram_mb as i32)
    .bind(&pin_map)
    .bind(&default_devices)
    .execute(db).await?;

    tracing::info!("Board SKU seeded: {}", board.sku);
    Ok(())
}
