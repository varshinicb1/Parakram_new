//! LLM Client — provider-agnostic.
//!
//! Priority (first match wins):
//!   1. OPENROUTER_API_KEY  → OpenRouter (primary — 100+ models, one key, cloud)
//!   2. ANTHROPIC_API_KEY   → Anthropic Claude (with prompt caching)
//!   3. LLM_BASE_URL        → any OpenAI-compatible endpoint (self-hosted backend only)
//!
//! OpenRouter is primary because the backend is cloud-hosted and serves
//! web + Android clients — "local" LLMs only apply to self-hosted deployments.

use serde::{Deserialize, Serialize};
use reqwest::Client;
use std::time::Instant;
use crate::ir::types::IRDocument;

// ── Shared types ─────────────────────────────────────────────────────────────

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct IntentResult {
    pub feasible: bool,
    #[serde(default)]
    pub reason: Option<String>,
    #[serde(default)]
    pub ir: Option<IRDocument>,
    #[serde(default)]
    pub clarifications: Option<Vec<String>>,
    #[serde(default)]
    pub suggestions: Option<Vec<String>>,
    pub model: String,
    pub generation_time_ms: u64,
}

#[derive(Debug, Deserialize)]
struct FeasibilityResponse {
    feasible: bool,
    #[serde(default)]
    reason: Option<String>,
    #[serde(default)]
    clarifications: Option<Vec<String>>,
    #[serde(default)]
    suggestions: Option<Vec<String>>,
}

// ── Anthropic request/response types ─────────────────────────────────────────

#[derive(Debug, Serialize)]
struct AnthropicRequest<'a> {
    model: &'a str,
    max_tokens: u32,
    system: Vec<AnthropicSystemBlock<'a>>,
    messages: Vec<AnthropicMessage<'a>>,
}

#[derive(Debug, Serialize)]
struct AnthropicSystemBlock<'a> {
    #[serde(rename = "type")]
    block_type: &'static str,
    text: &'a str,
    #[serde(skip_serializing_if = "Option::is_none")]
    cache_control: Option<AnthropicCacheControl>,
}

#[derive(Debug, Serialize)]
struct AnthropicCacheControl {
    #[serde(rename = "type")]
    cache_type: &'static str,
}

#[derive(Debug, Serialize)]
struct AnthropicMessage<'a> {
    role: &'static str,
    content: &'a str,
}

#[derive(Debug, Deserialize)]
struct AnthropicResponse {
    content: Vec<AnthropicContentBlock>,
    model: String,
}

#[derive(Debug, Deserialize)]
struct AnthropicContentBlock {
    #[serde(rename = "type")]
    block_type: String,
    text: Option<String>,
}

// ── OpenRouter request/response types (OpenAI-compatible) ───────────────────

#[derive(Debug, Serialize)]
struct OpenRouterRequest<'a> {
    model: &'a str,
    messages: Vec<OpenRouterMessage<'a>>,
    response_format: OpenRouterResponseFormat,
    temperature: f32,
    max_tokens: u32,
}

#[derive(Debug, Serialize)]
struct OpenRouterMessage<'a> {
    role: &'static str,
    content: &'a str,
}

#[derive(Debug, Serialize)]
struct OpenRouterResponseFormat {
    #[serde(rename = "type")]
    format_type: &'static str,
}

#[derive(Debug, Deserialize)]
struct OpenRouterResponse {
    choices: Vec<OpenRouterChoice>,
}

#[derive(Debug, Deserialize)]
struct OpenRouterChoice {
    message: OpenRouterChoiceMessage,
}

#[derive(Debug, Deserialize)]
struct OpenRouterChoiceMessage {
    content: String,
}

// ── Provider enum ─────────────────────────────────────────────────────────────

#[derive(Debug, Clone)]
enum Provider {
    /// Any OpenAI-compatible endpoint — Ollama, OpenAI, Groq, Mistral, LM Studio, etc.
    OpenAICompat { base_url: String, model: String },
    Anthropic { model: String },
    OpenRouter { model: String },
}

impl Provider {
    fn model_name(&self) -> &str {
        match self {
            Provider::OpenAICompat { model, .. } => model,
            Provider::Anthropic { model } => model,
            Provider::OpenRouter { model } => model,
        }
    }
}

// ── LLMClient ────────────────────────────────────────────────────────────────

pub struct LLMClient {
    http: Client,
    api_key: String,
    provider: Provider,
}

impl LLMClient {
    /// Resolve provider from environment. Priority:
    ///   1. OPENROUTER_API_KEY — primary for cloud deployments
    ///   2. ANTHROPIC_API_KEY  — alternative cloud option
    ///   3. LLM_BASE_URL       — self-hosted backend with local/private LLM
    pub fn new(api_key: String, model: String) -> Self {
        let (resolved_key, provider) = if let Ok(or_key) = std::env::var("OPENROUTER_API_KEY") {
            let mdl = std::env::var("OPENROUTER_MODEL")
                .unwrap_or(model);
            (or_key, Provider::OpenRouter { model: mdl })
        } else if let Ok(ant_key) = std::env::var("ANTHROPIC_API_KEY") {
            let mdl = std::env::var("ANTHROPIC_MODEL")
                .unwrap_or_else(|_| "claude-sonnet-4-6".to_string());
            (ant_key, Provider::Anthropic { model: mdl })
        } else if let Ok(base_url) = std::env::var("LLM_BASE_URL") {
            let key = std::env::var("LLM_API_KEY").unwrap_or_else(|_| "none".to_string());
            let mdl = std::env::var("LLM_MODEL").unwrap_or_else(|_| api_key);
            (key, Provider::OpenAICompat { base_url, model: mdl })
        } else {
            tracing::warn!("No LLM provider configured. Set OPENROUTER_API_KEY to enable intent processing.");
            ("".to_string(), Provider::OpenRouter { model: "openai/gpt-4o-mini".to_string() })
        };

        Self { http: Client::new(), api_key: resolved_key, provider }
    }

    /// Build a client using the user's own OpenRouter key directly.
    /// Used for BYOK — bypasses all env var logic.
    pub fn for_user_key(openrouter_key: String) -> Self {
        let model = std::env::var("OPENROUTER_MODEL")
            .unwrap_or_else(|_| "openai/gpt-4o-mini".to_string());
        Self {
            http: Client::new(),
            api_key: openrouter_key,
            provider: Provider::OpenRouter { model },
        }
    }

    /// Process a natural language intent (feasibility check → IR generation).
    pub async fn process_intent(
        &self,
        description: &str,
        _board_descriptor_json: &str,
        system_prompt: &str,
    ) -> Result<IntentResult, String> {
        let start = Instant::now();

        // Step 1: Feasibility check (small, fast call)
        let feasibility_prompt = format!(
            "Check feasibility: {}. Reply ONLY with JSON: {{\"feasible\": true/false, \"reason\": \"...\", \"clarifications\": [], \"suggestions\": []}}. Do NOT generate the IR document yet.",
            description
        );
        let feasibility_raw = self.call(system_prompt, &feasibility_prompt, 512).await?;

        let feasibility: FeasibilityResponse = serde_json::from_str(&feasibility_raw)
            .map_err(|e| format!("Failed to parse feasibility: {} — raw: {}", e, feasibility_raw))?;

        if !feasibility.feasible {
            return Ok(IntentResult {
                feasible: false,
                reason: feasibility.reason,
                ir: None,
                clarifications: feasibility.clarifications,
                suggestions: feasibility.suggestions,
                model: self.provider.model_name().to_string(),
                generation_time_ms: start.elapsed().as_millis() as u64,
            });
        }

        // Step 2: IR generation (larger call)
        let generation_prompt = format!(
            "Generate the full IR document for: {}. Reply with ONLY valid JSON matching the IRDocument schema.",
            description
        );
        let ir_raw = self.call(system_prompt, &generation_prompt, 4096).await?;

        let ir: IRDocument = match serde_json::from_str(&ir_raw) {
            Ok(ir) => ir,
            Err(e) => {
                tracing::warn!("LLM IR parse error (will retry): {}", e);
                let retry_prompt = format!(
                    "The IR JSON had errors: {}. Fix and regenerate the full IR for: {}. Return ONLY valid JSON.",
                    e, description
                );
                let retry_raw = self.call(system_prompt, &retry_prompt, 4096).await?;
                serde_json::from_str(&retry_raw)
                    .map_err(|e2| format!("IR generation failed after retry: {} — raw: {}", e2, retry_raw))?
            }
        };

        Ok(IntentResult {
            feasible: true,
            reason: None,
            ir: Some(ir),
            clarifications: None,
            suggestions: None,
            model: self.provider.model_name().to_string(),
            generation_time_ms: start.elapsed().as_millis() as u64,
        })
    }

    /// Generic completion — returns (raw_text, model_name).
    /// Used by endpoints that need free-form JSON responses (e.g. ROS graph generation).
    pub async fn raw_completion(
        &self,
        system_prompt: &str,
        user_message: &str,
    ) -> Result<(String, String), String> {
        let text = self.call(system_prompt, user_message, 4096).await?;
        Ok((text, self.provider.model_name().to_string()))
    }

    /// Dispatch to the correct provider.
    async fn call(&self, system_prompt: &str, user_message: &str, max_tokens: u32) -> Result<String, String> {
        match &self.provider {
            Provider::OpenAICompat { base_url, model } => {
                self.call_openai_compat(base_url, model, system_prompt, user_message, max_tokens).await
            }
            Provider::Anthropic { model } => {
                self.call_anthropic(model, system_prompt, user_message, max_tokens).await
            }
            Provider::OpenRouter { model } => {
                self.call_openrouter(model, system_prompt, user_message, max_tokens).await
            }
        }
    }

    /// OpenAI-compatible chat completions endpoint.
    /// Works with: OpenAI, Ollama, Groq, Mistral, LM Studio, Together, any local model.
    async fn call_openai_compat(
        &self,
        base_url: &str,
        model: &str,
        system_prompt: &str,
        user_message: &str,
        max_tokens: u32,
    ) -> Result<String, String> {
        let url = format!("{}/chat/completions", base_url.trim_end_matches('/'));
        let request = OpenRouterRequest {
            model,
            messages: vec![
                OpenRouterMessage { role: "system", content: system_prompt },
                OpenRouterMessage { role: "user",   content: user_message  },
            ],
            response_format: OpenRouterResponseFormat { format_type: "json_object" },
            temperature: 0.1,
            max_tokens,
        };

        let mut req = self.http.post(&url).json(&request);
        if self.api_key != "none" && !self.api_key.is_empty() {
            req = req.header("Authorization", format!("Bearer {}", self.api_key));
        }

        let response = req.send().await
            .map_err(|e| format!("LLM request to {} failed: {}", url, e))?;

        if !response.status().is_success() {
            let status = response.status();
            let body = response.text().await.unwrap_or_default();
            return Err(format!("LLM API error {} from {}: {}", status, url, body));
        }

        let parsed: OpenRouterResponse = response.json().await
            .map_err(|e| format!("Failed to parse LLM response: {}", e))?;

        let raw = parsed.choices.first()
            .map(|c| c.message.content.clone())
            .ok_or_else(|| "No response from LLM".to_string())?;

        Ok(strip_markdown_fences(raw))
    }

    /// Anthropic Messages API with prompt caching on the system prompt.
    async fn call_anthropic(
        &self,
        model: &str,
        system_prompt: &str,
        user_message: &str,
        max_tokens: u32,
    ) -> Result<String, String> {
        let request = AnthropicRequest {
            model,
            max_tokens,
            // The system prompt is cached after the first call — saves ~70% token cost
            // on subsequent requests since it contains the full driver registry.
            system: vec![AnthropicSystemBlock {
                block_type: "text",
                text: system_prompt,
                cache_control: Some(AnthropicCacheControl { cache_type: "ephemeral" }),
            }],
            messages: vec![AnthropicMessage {
                role: "user",
                content: user_message,
            }],
        };

        let response = self.http
            .post("https://api.anthropic.com/v1/messages")
            .header("x-api-key", &self.api_key)
            .header("anthropic-version", "2023-06-01")
            .header("anthropic-beta", "prompt-caching-2024-07-31")
            .header("content-type", "application/json")
            .json(&request)
            .send()
            .await
            .map_err(|e| format!("Anthropic request failed: {}", e))?;

        if !response.status().is_success() {
            let status = response.status();
            let body = response.text().await.unwrap_or_default();
            return Err(format!("Anthropic API error {}: {}", status, body));
        }

        let parsed: AnthropicResponse = response.json().await
            .map_err(|e| format!("Failed to parse Anthropic response: {}", e))?;

        let text = parsed.content
            .into_iter()
            .find(|b| b.block_type == "text")
            .and_then(|b| b.text)
            .ok_or_else(|| "No text content in Anthropic response".to_string())?;

        Ok(strip_markdown_fences(text))
    }

    /// OpenRouter API (OpenAI-compatible format).
    async fn call_openrouter(
        &self,
        model: &str,
        system_prompt: &str,
        user_message: &str,
        max_tokens: u32,
    ) -> Result<String, String> {
        let request = OpenRouterRequest {
            model,
            messages: vec![
                OpenRouterMessage { role: "system", content: system_prompt },
                OpenRouterMessage { role: "user", content: user_message },
            ],
            response_format: OpenRouterResponseFormat { format_type: "json_object" },
            temperature: 0.1,
            max_tokens,
        };

        let response = self.http
            .post("https://openrouter.ai/api/v1/chat/completions")
            .header("Authorization", format!("Bearer {}", self.api_key))
            .header("Content-Type", "application/json")
            .json(&request)
            .send()
            .await
            .map_err(|e| format!("OpenRouter request failed: {}", e))?;

        if !response.status().is_success() {
            let status = response.status();
            let body = response.text().await.unwrap_or_default();
            return Err(format!("OpenRouter API error {}: {}", status, body));
        }

        let parsed: OpenRouterResponse = response.json().await
            .map_err(|e| format!("Failed to parse OpenRouter response: {}", e))?;

        let raw = parsed.choices
            .first()
            .map(|c| c.message.content.clone())
            .ok_or_else(|| "No response from OpenRouter".to_string())?;

        Ok(strip_markdown_fences(raw))
    }
}

fn strip_markdown_fences(raw: String) -> String {
    let mut s = raw.trim().to_string();
    if s.starts_with("```json") {
        s = s[7..].to_string();
    } else if s.starts_with("```") {
        s = s[3..].to_string();
    }
    if s.ends_with("```") {
        s = s[..s.len() - 3].to_string();
    }
    s.trim().to_string()
}
