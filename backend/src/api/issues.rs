//! Issue Reporting API — forwards user-submitted bug reports
//! directly to varshinicb@vidyuthlabs.co.in via the EmailService.

use axum::{
    extract::State,
    http::StatusCode,
    routing::post,
    Json, Router,
};
use serde::{Deserialize, Serialize};

use crate::AppState;

pub fn router() -> Router<AppState> {
    Router::new()
        .route("/report", post(report_issue))
}

#[derive(Debug, Deserialize)]
pub struct IssueReport {
    pub title: String,
    pub description: String,
    pub reporter_email: Option<String>,
    pub page: Option<String>,
    pub severity: Option<String>,
}

#[derive(Debug, Serialize)]
pub struct IssueResponse {
    pub status: String,
    pub message: String,
}

async fn report_issue(
    State(state): State<AppState>,
    Json(report): Json<IssueReport>,
) -> Result<Json<IssueResponse>, (StatusCode, String)> {
    if report.title.trim().is_empty() || report.description.trim().is_empty() {
        return Err((StatusCode::BAD_REQUEST, "Title and description are required".into()));
    }

    let severity = report.severity.as_deref().unwrap_or("medium");
    let reporter = report.reporter_email.as_deref().unwrap_or("anonymous");
    let page = report.page.as_deref().unwrap_or("unknown");

    let subject = format!("[Parakram Issue] [{}] {}", severity.to_uppercase(), report.title);
    let body = format!(
        "NEW ISSUE REPORT\n\
         ================\n\n\
         Title:       {}\n\
         Severity:    {}\n\
         Reporter:    {}\n\
         Page:        {}\n\n\
         Description:\n{}\n\n\
         ---\n\
         Sent automatically by Parakram Issue Reporter",
        report.title, severity, reporter, page, report.description
    );

    // Send to the founder's email
    let target_email = "varshinicb@vidyuthlabs.co.in";

    match state.email_svc.send_issue_report(target_email, &subject, &body).await {
        Ok(()) => {
            tracing::info!(
                title = %report.title,
                reporter = %reporter,
                severity = %severity,
                "Issue report forwarded to {}", target_email
            );
            Ok(Json(IssueResponse {
                status: "submitted".into(),
                message: "Thank you! Your issue has been reported and will be reviewed shortly.".into(),
            }))
        }
        Err(e) => {
            tracing::error!("Failed to send issue report email: {}", e);
            // Still return success to user — we log it server-side
            Ok(Json(IssueResponse {
                status: "logged".into(),
                message: "Issue logged internally. Email delivery pending.".into(),
            }))
        }
    }
}
