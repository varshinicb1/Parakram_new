//! Keyword-based template classifier.
//!
//! Scores every template against a user prompt using weighted keyword matching.
//! Designed to be swapped for an ONNX sentence-transformer in Phase 2 without
//! changing any calling code — just replace `score_all()`.

use super::templates::{all_templates, GraphTemplate};

/// Score one template against the lowercased prompt.
fn score(template: &GraphTemplate, prompt_lower: &str) -> f32 {
    let mut total: f32 = 0.0;
    let mut max_possible: f32 = 0.0;

    for (keyword, weight) in template.keywords {
        max_possible += weight;
        if prompt_lower.contains(keyword) {
            total += weight;
        }
    }

    // Normalise to 0.0–1.0
    if max_possible > 0.0 { total / max_possible } else { 0.0 }
}

/// Returns all templates sorted by descending score. Score of 0.0 means no match.
pub fn score_all(prompt: &str) -> Vec<(GraphTemplate, f32)> {
    let lower = prompt.to_lowercase();
    let mut scored: Vec<(GraphTemplate, f32)> = all_templates()
        .into_iter()
        .map(|t| {
            let s = score(&t, &lower);
            (t, s)
        })
        .collect();

    scored.sort_by(|a, b| b.1.partial_cmp(&a.1).unwrap_or(std::cmp::Ordering::Equal));
    scored
}

/// Pick the templates to actually include in the graph.
///
/// Rules:
/// - Always include the top scorer if it scored > 0.
/// - Include additional templates if they scored > 50% of the top score AND > 0.05.
/// - Cap at 4 templates to avoid bloated graphs.
/// - If nothing scored, fall back to a generic sensor-monitoring template.
pub fn select_templates(scored: &[(GraphTemplate, f32)]) -> Vec<GraphTemplate> {
    if scored.is_empty() {
        return fallback_templates();
    }

    let (top_template, top_score) = &scored[0];
    if *top_score == 0.0 {
        return fallback_templates();
    }

    let threshold = (top_score * 0.5).max(0.05);

    let mut selected: Vec<GraphTemplate> = scored
        .iter()
        .filter(|(_, s)| *s >= threshold)
        .take(4)
        .map(|(t, _)| t.clone())
        .collect();

    // Always ensure device_manager is present if we have Parakram devices
    let has_device_manager = selected.iter().any(|t| {
        (t.nodes)().iter().any(|n| n.executable == "device_manager")
    });
    if !has_device_manager {
        if let Some(base) = scored.iter().find(|(t, _)| t.name == "sensor_monitoring") {
            selected.push(base.0.clone());
        }
    }

    // Ensure top template is first
    let top_name = top_template.name;
    selected.sort_by(|a, _| if a.name == top_name { std::cmp::Ordering::Less } else { std::cmp::Ordering::Greater });

    selected
}

fn fallback_templates() -> Vec<GraphTemplate> {
    all_templates()
        .into_iter()
        .filter(|t| t.name == "sensor_monitoring")
        .collect()
}
