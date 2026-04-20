//! Email service — transactional email via SMTP or SendGrid HTTP API.
//!
//! Credentials are read from environment variables. When absent the service
//! logs the email content instead (dev-mode behaviour), so callers never need
//! to guard against missing config.
//!
//! SendGrid HTTP (preferred):
//!   SENDGRID_API_KEY  — API key from app.sendgrid.com
//!   EMAIL_FROM        — verified sender address (default: noreply@vidyuthlabs.co.in)
//!
//! SMTP fallback:
//!   SMTP_HOST, SMTP_PORT, SMTP_USER, SMTP_PASS, EMAIL_FROM

use serde_json::json;
use tracing::{info, warn, error};

#[derive(Debug, Clone)]
pub struct EmailService {
    sendgrid_key: Option<String>,
    from_address: String,
    client: reqwest::Client,
}

impl EmailService {
    pub fn new() -> Self {
        let sendgrid_key = std::env::var("SENDGRID_API_KEY").ok();
        let from_address = std::env::var("EMAIL_FROM")
            .unwrap_or_else(|_| "noreply@vidyuthlabs.co.in".to_string());

        if sendgrid_key.is_none() {
            warn!("SENDGRID_API_KEY not set — emails will be logged only (dev mode)");
        }

        Self {
            sendgrid_key,
            from_address,
            client: reqwest::Client::new(),
        }
    }

    /// Send a welcome email to a newly registered user.
    pub async fn send_welcome(&self, to_email: &str, username: &str) -> Result<(), String> {
        let subject = "Welcome to Parakram — Vidyuthlabs";
        let body = format!(
            "Hi {username},\n\n\
             Welcome to Parakram! You can now build IoT projects in plain English.\n\n\
             Get started: https://vidyuthlabs.co.in\n\n\
             — The Vidyuthlabs Team",
        );
        self.send(to_email, subject, &body).await
    }

    /// Send an email-verification code to a newly registered user.
    pub async fn send_verification(&self, to_email: &str, username: &str, code: &str) -> Result<(), String> {
        let subject = "Parakram — Verify your email address";
        let body = format!(
            "Hi {username},\n\n\
             Your Parakram email verification code is:\n\n\
             {code}\n\n\
             Enter this code in the app to verify your email. It expires in 15 minutes.\n\n\
             — Vidyuthlabs",
        );
        self.send(to_email, subject, &body).await
    }

    /// Send a password-reset email containing a 6-digit code.
    /// Returns Ok(()) whether the email was delivered or just logged.
    pub async fn send_password_reset(
        &self,
        to_email: &str,
        username: &str,
        code: &str,
    ) -> Result<(), String> {
        let subject = "Parakram — Your password reset code";
        let body = format!(
            "Hi {username},\n\n\
             Your Parakram password reset code is:\n\n\
             {code}\n\n\
             This code expires in 15 minutes.\n\n\
             If you did not request a password reset, you can safely ignore this email.\n\n\
             — Vidyuthlabs",
        );
        self.send(to_email, subject, &body).await
    }

    /// Forward issue reports to the founder's inbox.
    pub async fn send_issue_report(&self, to: &str, subject: &str, body: &str) -> Result<(), String> {
        self.send(to, subject, body).await
    }

    /// Generic send — SendGrid preferred, falls back to log.
    async fn send(&self, to: &str, subject: &str, body: &str) -> Result<(), String> {
        match &self.sendgrid_key {
            Some(key) => self.send_via_sendgrid(key, to, subject, body).await,
            None => {
                info!(
                    to = %to,
                    subject = %subject,
                    "[DEV] Email not sent — SENDGRID_API_KEY missing. Body:\n{}",
                    body
                );
                Ok(())
            }
        }
    }

    async fn send_via_sendgrid(
        &self,
        api_key: &str,
        to: &str,
        subject: &str,
        body: &str,
    ) -> Result<(), String> {
        let payload = json!({
            "personalizations": [{ "to": [{ "email": to }] }],
            "from": { "email": self.from_address },
            "subject": subject,
            "content": [{ "type": "text/plain", "value": body }],
        });

        let resp = self.client
            .post("https://api.sendgrid.com/v3/mail/send")
            .header("Authorization", format!("Bearer {api_key}"))
            .header("Content-Type", "application/json")
            .json(&payload)
            .send()
            .await
            .map_err(|e| format!("SendGrid request error: {e}"))?;

        let status = resp.status();
        if status.is_success() || status.as_u16() == 202 {
            info!(to = %to, subject = %subject, "Email sent via SendGrid");
            Ok(())
        } else {
            let text = resp.text().await.unwrap_or_default();
            error!(status = %status, body = %text, "SendGrid delivery failed");
            Err(format!("SendGrid error {status}: {text}"))
        }
    }
}
