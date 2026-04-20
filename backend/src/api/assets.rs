//! Asset Conversion Endpoint
//! Accepts image uploads (PNG/JPEG) and converts them into Parakram 16-bit RGB565 Blob structures.

use axum::{extract::State, http::StatusCode, routing::post, Json, Router};
use axum::extract::Multipart;
use serde::Serialize;
use image::{GenericImageView, DynamicImage};

use crate::AppState;

pub fn router() -> Router<AppState> {
    Router::new()
        .route("/convert", post(convert_image))
}

#[derive(Serialize)]
pub struct AssetConversionResult {
    pub id: String,
    pub width: u32,
    pub height: u32,
    pub format: String,
    pub blob_base64: String,
}

#[derive(Serialize)]
pub struct ErrorBody {
    pub error: String,
}

async fn convert_image(
    State(_state): State<AppState>,
    mut multipart: Multipart,
) -> Result<Json<AssetConversionResult>, (StatusCode, Json<ErrorBody>)> {
    
    let mut image_data = Vec::new();
    
    while let Some(field) = multipart.next_field().await.unwrap_or(None) {
        let name = field.name().unwrap_or("").to_string();
        if name == "file" || name == "image" {
            let data = field.bytes().await.map_err(|_| {
                (StatusCode::BAD_REQUEST, Json(ErrorBody { error: "Failed to read body data".into() }))
            })?;
            image_data.extend_from_slice(&data);
            break;
        }
    }
    
    if image_data.is_empty() {
        return Err((StatusCode::BAD_REQUEST, Json(ErrorBody { error: "No image file provided".into() })));
    }

    // Decode image via the image crate
    let img = image::load_from_memory(&image_data).map_err(|e| {
        (StatusCode::UNPROCESSABLE_ENTITY, Json(ErrorBody { error: format!("Invalid image format: {}", e) }))
    })?;

    // We constrain UI assets to a max display dimension to prevent RAM overflow
    let (w, h) = img.dimensions();
    if w > 320 || h > 480 {
        return Err((StatusCode::PAYLOAD_TOO_LARGE, Json(ErrorBody { error: "Image dimensions exceed 320x480 max bounds".into() })));
    }

    // Convert to LVGL RGB565 (16-bit color depth with swapped endianness for SPI)
    let rgb565_bytes = convert_to_rgb565(&img);
    
    // Generate a unique ID
    let img_id = format!("img_{}", &uuid::Uuid::new_v4().to_string()[..6]);

    let base64_payload = base64::Engine::encode(
        &base64::engine::general_purpose::STANDARD_NO_PAD, 
        &rgb565_bytes
    );

    Ok(Json(AssetConversionResult {
        id: img_id,
        width: w,
        height: h,
        format: "RGB565".into(),
        blob_base64: base64_payload,
    }))
}

/// Helper: converts generic image into an array of 16-bit RGB565 bytes.
/// ESP32-S3 SPI displays (ST7789/ILI9341) typically expect byte-swapped transfers.
fn convert_to_rgb565(img: &DynamicImage) -> Vec<u8> {
    let rgb_img = img.to_rgb8();
    let mut out = Vec::with_capacity((img.width() * img.height() * 2) as usize);

    for pixel in rgb_img.pixels() {
        let r = pixel[0] as u16;
        let g = pixel[1] as u16;
        let b = pixel[2] as u16;

        let r5 = (r >> 3) & 0x1F;
        let g6 = (g >> 2) & 0x3F;
        let b5 = (b >> 3) & 0x1F;

        // RGB565 layout: rrrrrggg gggbbbbb
        let rgb16 = (r5 << 11) | (g6 << 5) | b5;

        // Note: For LVGL + ESP SPI, we often use LV_COLOR_16_SWAP = 1.
        // We'll swap bytes here so LVGL can just stream them.
        let byte_low = (rgb16 & 0xFF) as u8;
        let byte_high = ((rgb16 >> 8) & 0xFF) as u8;
        
        // Outputting mapped SWAPPED bytes format directly
        out.push(byte_high);
        out.push(byte_low);
    }

    out
}
