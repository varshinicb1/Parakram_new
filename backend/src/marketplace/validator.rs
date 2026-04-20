//! Static analysis for community driver submissions.
//!
//! Validates that submitted C source:
//! 1. Is within size limits
//! 2. Has no forbidden patterns (shell execution, dynamic loading, unsafe memory)
//! 3. Contains the required Parakram driver_vtable_t symbol
//! 4. Extracts bus types and capabilities for the database
//!
//! No subprocess, no compilation — pure pattern scanning over `&str`.
//! Uses only `std`; no external crates.

// ── Maximum accepted source sizes ────────────────────────────────────────────
const MAX_SOURCE_BYTES: usize = 64 * 1024; // 64 KB
const MAX_SOURCE_LINES: usize = 2_000;

// ── Forbidden OS-level call patterns ─────────────────────────────────────────
const FORBIDDEN_CALLS: &[&str] = &[
    "system(",
    "popen(",
    "exec(",
    "execv(",
    "execve(",
    "fork(",
    "dlopen(",
    "dlsym(",
];

// ── Bus-type detection: (probe strings, canonical name) ──────────────────────
const BUS_PROBES: &[(&[&str], &str)] = &[
    (&["pal_i2c_", "i2c_bus_read", "i2c_bus_write"], "i2c"),
    (&["pal_spi_"], "spi"),
    (&["pal_gpio_"], "gpio"),
    (&["pal_uart_"], "uart"),
    (&["pal_pwm_"], "pwm"),
    (&["pal_adc_"], "adc"),
    (&["pal_onewire_"], "onewire"),
    (&["pal_i2s_"], "i2s"),
];

// ── Result type returned by `validate` ───────────────────────────────────────

/// The outcome of static analysis on a community-driver submission.
///
/// `passed` is `true` only when `errors` is empty.  `warnings` are advisory
/// and do not block submission from being accepted by a human moderator.
#[derive(Debug, Clone)]
pub struct ValidationResult {
    /// `true` iff there are no hard errors.
    pub passed: bool,
    /// Hard failures — submission must be corrected before it can be accepted.
    pub errors: Vec<String>,
    /// Non-blocking advisories the author should address.
    pub warnings: Vec<String>,
    /// Bus types inferred from PAL call patterns in the source.
    pub detected_bus_types: Vec<String>,
    /// All unique `CAP_*` identifiers found in the source.
    pub detected_capabilities: Vec<String>,
    /// The driver name extracted from the `driver_vtable_t drv_XXX_vtable` declaration.
    pub detected_name: Option<String>,
    /// Number of non-empty lines in the source (for informational display).
    pub source_lines: usize,
}

// ── Public API ────────────────────────────────────────────────────────────────

/// Run all static checks on `source` and return a [`ValidationResult`].
///
/// # Arguments
/// * `source`         – Raw C source text submitted by the user.
/// * `submitted_name` – The `name` field the user chose, e.g. `"drv_my_sensor"`.
/// * `known_names`    – Slice of names already in the official Vidyuthlabs registry;
///                      any collision is a hard error.
pub fn validate(source: &str, submitted_name: &str, known_names: &[&str]) -> ValidationResult {
    let mut errors: Vec<String> = Vec::new();
    let mut warnings: Vec<String> = Vec::new();

    // ── Size checks ───────────────────────────────────────────────────────────
    if source.len() > MAX_SOURCE_BYTES {
        errors.push("source exceeds 64 KB limit".into());
    }

    let source_lines = source.lines().count();
    if source_lines > MAX_SOURCE_LINES {
        errors.push("source exceeds 2000 line limit".into());
    }

    // ── Name format checks ────────────────────────────────────────────────────
    if !submitted_name.starts_with("drv_") {
        errors.push("name must start with drv_".into());
    }

    if !submitted_name.chars().all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == '_') {
        errors.push("name contains invalid characters".into());
    }

    if known_names.contains(&submitted_name) {
        errors.push("name conflict with official driver".into());
    }

    // ── Forbidden OS-level calls ──────────────────────────────────────────────
    for &call in FORBIDDEN_CALLS {
        if source.contains(call) {
            errors.push(format!("forbidden: OS-level call found ({})", call.trim_end_matches('(')));
            break; // one error per category is enough; each unique call is still caught below
        }
    }
    // Re-run to report each distinct forbidden call (so the author sees all of them).
    // We already pushed one error above on the first match; collect additional ones here
    // only if there are multiple distinct violations so the first error is not duplicated.
    {
        let mut found: Vec<&str> = FORBIDDEN_CALLS
            .iter()
            .copied()
            .filter(|&call| source.contains(call))
            .collect();

        // If more than one distinct call was found we replace the single combined error
        // with individual entries so the author can address each one.
        if found.len() > 1 {
            // Remove the generic error we already pushed
            errors.retain(|e| !e.starts_with("forbidden: OS-level call found"));
            for call in found.drain(..) {
                errors.push(format!(
                    "forbidden: OS-level call found ({})",
                    call.trim_end_matches('(')
                ));
            }
        }
    }

    // ── Heap allocation via stdlib.h ──────────────────────────────────────────
    let has_stdlib_h = source.contains("#include <stdlib.h>");
    let has_heap_via_stdlib = has_stdlib_h
        && (source.contains("malloc(") || source.contains("calloc(") || source.contains("realloc("));

    if has_heap_via_stdlib {
        errors.push("forbidden: heap allocation via stdlib.h".into());
    }

    // ── Path traversal ────────────────────────────────────────────────────────
    if source.contains("../../") || source.contains("../..") {
        errors.push("forbidden: path traversal in source".into());
    }

    // ── Required vtable symbol ────────────────────────────────────────────────
    if !source.contains("driver_vtable_t") {
        errors.push("missing: driver_vtable_t symbol".into());
    }

    // ── Extract driver name from vtable declaration ───────────────────────────
    let detected_name = extract_vtable_name(source);

    // Name mismatch check (only meaningful when vtable was found)
    if source.contains("driver_vtable_t") {
        match &detected_name {
            Some(vtable_name) => {
                // submitted_name is e.g. "drv_foo"; vtable_name is "foo" (inner capture)
                let expected_inner = submitted_name.strip_prefix("drv_").unwrap_or(submitted_name);
                if vtable_name != expected_inner {
                    errors.push(format!(
                        "name mismatch: submitted '{}' but vtable declares 'drv_{}_vtable'",
                        submitted_name, vtable_name
                    ));
                }
            }
            None => {
                // Symbol keyword present but pattern not parseable — warn only if no
                // other vtable error already recorded.
                if !errors.iter().any(|e| e.contains("driver_vtable_t")) {
                    warnings.push(
                        "driver_vtable_t found but could not parse drv_<name>_vtable declaration"
                            .into(),
                    );
                }
            }
        }
    }

    // ── Warnings: heap without stdlib.h ──────────────────────────────────────
    if !has_stdlib_h && (source.contains("malloc(") || source.contains("calloc(")) {
        warnings.push(
            "heap allocation detected — use static buffers for embedded".into(),
        );
    }

    // ── Warnings: printf / fprintf ────────────────────────────────────────────
    if source.contains("printf(") || source.contains("fprintf(") {
        warnings.push("printf found — use pal_log() instead for cross-platform logging".into());
    }

    // ── Warnings: missing lifecycle functions ─────────────────────────────────
    if !source.contains("_init") {
        warnings.push("missing init function".into());
    }
    if !source.contains("_deinit") {
        warnings.push("missing deinit function".into());
    }
    if !source.contains("_read") && !source.contains("_write") {
        warnings.push("missing read/write function".into());
    }

    // ── Bus-type detection ────────────────────────────────────────────────────
    let detected_bus_types = detect_bus_types(source);

    // ── Capability detection ──────────────────────────────────────────────────
    let detected_capabilities = detect_capabilities(source);

    let passed = errors.is_empty();

    ValidationResult {
        passed,
        errors,
        warnings,
        detected_bus_types,
        detected_capabilities,
        detected_name,
        source_lines,
    }
}

// ── Private helpers ───────────────────────────────────────────────────────────

/// Scan `source` for bus-type PAL call patterns and return the matched
/// canonical bus-type names, deduplicated and sorted.
fn detect_bus_types(source: &str) -> Vec<String> {
    let mut found: Vec<String> = Vec::new();
    for &(probes, canonical) in BUS_PROBES {
        if probes.iter().any(|&p| source.contains(p)) {
            found.push(canonical.to_string());
        }
    }
    found // already in stable definition order; no sort needed for determinism
}

/// Collect all unique `CAP_[A-Z0-9_]+` tokens from `source`.
///
/// This is a manual scan: we walk the source byte-by-byte looking for the
/// literal prefix `CAP_` then consume subsequent `[A-Z0-9_]` characters.
/// Results are deduplicated and returned in first-occurrence order.
fn detect_capabilities(source: &str) -> Vec<String> {
    let bytes = source.as_bytes();
    let len = bytes.len();
    let mut caps: Vec<String> = Vec::new();
    let mut i = 0usize;

    while i + 4 <= len {
        // Fast prefix match for 'C','A','P','_'
        if bytes[i] == b'C'
            && bytes[i + 1] == b'A'
            && bytes[i + 2] == b'P'
            && bytes[i + 3] == b'_'
        {
            // Ensure we're at a token boundary — the byte before must not be
            // an identifier character so we don't match e.g. "NOCAP_FOO".
            let at_boundary = i == 0 || !is_ident_byte(bytes[i - 1]);
            if at_boundary {
                let start = i;
                i += 4; // skip "CAP_"
                // Consume remaining [A-Z0-9_] characters
                while i < len && is_cap_body_byte(bytes[i]) {
                    i += 1;
                }
                // Must have at least one character after the prefix
                if i > start + 4 {
                    // Safety: source is valid UTF-8; all consumed bytes are ASCII.
                    let token = &source[start..i];
                    if !caps.iter().any(|c| c == token) {
                        caps.push(token.to_string());
                    }
                }
                continue;
            }
        }
        i += 1;
    }

    caps
}

/// Extract the inner name from a vtable declaration such as:
///
/// ```c
/// driver_vtable_t drv_foo_vtable = { ... };
/// const driver_vtable_t drv_bar_vtable = { ... };
/// ```
///
/// Returns `Some("foo")` / `Some("bar")` respectively, or `None` if no
/// parseable declaration is found.
fn extract_vtable_name(source: &str) -> Option<String> {
    // We scan for the pattern:
    //   "driver_vtable_t" <whitespace> ["const" <whitespace>] "drv_" <name> "_vtable"
    // where <name> is [a-z0-9_]+.
    //
    // To avoid a regex dependency we walk through the source looking for
    // occurrences of "driver_vtable_t" and then parse forward manually.

    let mut search_from = 0usize;

    while let Some(rel) = source[search_from..].find("driver_vtable_t") {
        let pos = search_from + rel;
        let after_keyword = &source[pos + "driver_vtable_t".len()..];

        // Skip mandatory whitespace
        let after_ws = after_keyword.trim_start_matches(|c: char| c == ' ' || c == '\t' || c == '\n' || c == '\r');

        // Optional "const" that may appear AFTER the type keyword in some styles
        // e.g. "driver_vtable_t const drv_foo_vtable"
        let after_const = if after_ws.starts_with("const") {
            after_ws["const".len()..].trim_start_matches(|c: char| c.is_ascii_whitespace())
        } else {
            after_ws
        };

        // Now we expect "drv_<name>_vtable".
        // Strategy: find the first occurrence of "_vtable" in the remainder and
        // verify everything between "drv_" and "_vtable" is a valid name token.
        if let Some(rest) = after_const.strip_prefix("drv_") {
            // Locate "_vtable" within the rest; it must appear before any whitespace
            // that would indicate the end of the declarator identifier.
            if let Some(vtable_pos) = rest.find("_vtable") {
                let inner = &rest[..vtable_pos];
                // inner must be non-empty, contain only [a-z0-9_], and the char
                // immediately after "_vtable" must not be an identifier character
                // (so we don't match "drv_foo_vtable_ext").
                let after_vtable = &rest[vtable_pos + "_vtable".len()..];
                let next_is_boundary = after_vtable
                    .chars()
                    .next()
                    .map(|c| !c.is_ascii_alphanumeric() && c != '_')
                    .unwrap_or(true); // end of string is also a boundary

                let inner_valid = !inner.is_empty()
                    && inner.chars().all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == '_');

                if inner_valid && next_is_boundary {
                    return Some(inner.to_string());
                }
            }
        }

        // Advance past this occurrence and keep searching
        search_from = pos + "driver_vtable_t".len();
    }

    None
}

/// Returns `true` for bytes that are valid C identifier continuation characters
/// (used to detect token boundaries before `CAP_`).
#[inline(always)]
fn is_ident_byte(b: u8) -> bool {
    b.is_ascii_alphanumeric() || b == b'_'
}

/// Returns `true` for bytes valid inside a `CAP_*` token body: `[A-Z0-9_]`.
#[inline(always)]
fn is_cap_body_byte(b: u8) -> bool {
    b.is_ascii_uppercase() || b.is_ascii_digit() || b == b'_'
}

// ── Unit tests ────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    // ── Helper: build a minimal valid driver source ───────────────────────────
    fn minimal_source(name: &str) -> String {
        format!(
            r#"
#include "parakram/pal.h"

static int {name}_init(void) {{ return 0; }}
static int {name}_deinit(void) {{ return 0; }}
static int {name}_read(void *buf, size_t len) {{ return 0; }}

void pal_i2c_transfer(void) {{}}

#define CAP_TEMPERATURE 1
#define CAP_HUMIDITY    2

driver_vtable_t drv_{name}_vtable = {{
    .init   = {name}_init,
    .deinit = {name}_deinit,
    .read   = {name}_read,
}};
"#,
            name = name
        )
    }

    // ── Basic happy path ──────────────────────────────────────────────────────

    #[test]
    fn valid_driver_passes() {
        let src = minimal_source("bme999");
        let result = validate(&src, "drv_bme999", &["drv_bme280", "drv_bme680"]);
        assert!(result.passed, "errors: {:?}", result.errors);
        assert!(result.errors.is_empty());
    }

    // ── Size limits ───────────────────────────────────────────────────────────

    #[test]
    fn rejects_oversized_source() {
        let huge = "x".repeat(MAX_SOURCE_BYTES + 1);
        let result = validate(&huge, "drv_big", &[]);
        assert!(!result.passed);
        assert!(result.errors.iter().any(|e| e.contains("64 KB")));
    }

    #[test]
    fn rejects_too_many_lines() {
        let lines = "int x;\n".repeat(MAX_SOURCE_LINES + 1);
        let result = validate(&lines, "drv_long", &[]);
        assert!(!result.passed);
        assert!(result.errors.iter().any(|e| e.contains("2000 line")));
    }

    // ── Name validation ───────────────────────────────────────────────────────

    #[test]
    fn rejects_missing_drv_prefix() {
        let src = minimal_source("foo");
        let result = validate(&src, "foo_sensor", &[]);
        assert!(result.errors.iter().any(|e| e.contains("must start with drv_")));
    }

    #[test]
    fn rejects_invalid_chars_in_name() {
        let src = minimal_source("foo");
        let result = validate(&src, "drv_Foo-bar", &[]);
        assert!(result.errors.iter().any(|e| e.contains("invalid characters")));
    }

    #[test]
    fn rejects_name_collision_with_official() {
        let src = minimal_source("bme280");
        let result = validate(&src, "drv_bme280", &["drv_bme280"]);
        assert!(result.errors.iter().any(|e| e.contains("name conflict")));
    }

    // ── Forbidden calls ───────────────────────────────────────────────────────

    #[test]
    fn rejects_system_call() {
        let src = format!("{}\nsystem(\"ls\");", minimal_source("s1"));
        let result = validate(&src, "drv_s1", &[]);
        assert!(result.errors.iter().any(|e| e.contains("OS-level call")));
    }

    #[test]
    fn rejects_dlopen() {
        let src = format!("{}\ndlopen(\"lib.so\", 0);", minimal_source("s2"));
        let result = validate(&src, "drv_s2", &[]);
        assert!(result.errors.iter().any(|e| e.contains("OS-level call")));
    }

    // ── Heap via stdlib.h ─────────────────────────────────────────────────────

    #[test]
    fn rejects_malloc_with_stdlib() {
        let src = format!("{}\n#include <stdlib.h>\nvoid *p = malloc(64);", minimal_source("h1"));
        let result = validate(&src, "drv_h1", &[]);
        assert!(result.errors.iter().any(|e| e.contains("heap allocation via stdlib.h")));
    }

    #[test]
    fn warns_malloc_without_stdlib() {
        // malloc without #include <stdlib.h> is a warning, not an error
        let src = format!("{}\nvoid *p = malloc(64);", minimal_source("h2"));
        let result = validate(&src, "drv_h2", &[]);
        assert!(!result.errors.iter().any(|e| e.contains("heap allocation via stdlib.h")));
        assert!(result.warnings.iter().any(|w| w.contains("heap allocation detected")));
    }

    // ── Path traversal ────────────────────────────────────────────────────────

    #[test]
    fn rejects_path_traversal() {
        let src = format!("{}\n#include \"../../secret.h\"", minimal_source("pt1"));
        let result = validate(&src, "drv_pt1", &[]);
        assert!(result.errors.iter().any(|e| e.contains("path traversal")));
    }

    // ── Missing vtable ────────────────────────────────────────────────────────

    #[test]
    fn rejects_missing_vtable() {
        let src = "static int x_init(void) { return 0; }\n";
        let result = validate(src, "drv_x", &[]);
        assert!(result.errors.iter().any(|e| e.contains("driver_vtable_t")));
    }

    // ── Name mismatch ─────────────────────────────────────────────────────────

    #[test]
    fn rejects_vtable_name_mismatch() {
        // Source declares drv_bme999_vtable but submitted name is drv_other
        let src = minimal_source("bme999");
        let result = validate(&src, "drv_other", &[]);
        assert!(result.errors.iter().any(|e| e.contains("name mismatch")));
    }

    // ── printf warning ────────────────────────────────────────────────────────

    #[test]
    fn warns_printf() {
        let src = format!("{}\nprintf(\"hello\");", minimal_source("p1"));
        let result = validate(&src, "drv_p1", &[]);
        assert!(result.warnings.iter().any(|w| w.contains("printf found")));
    }

    // ── Missing lifecycle warnings ────────────────────────────────────────────

    #[test]
    fn warns_missing_init() {
        // Source with no _init substring
        let src = "driver_vtable_t drv_ni_vtable;\n";
        let result = validate(src, "drv_ni", &[]);
        assert!(result.warnings.iter().any(|w| w.contains("missing init")));
    }

    #[test]
    fn warns_missing_read_write() {
        let src = "driver_vtable_t drv_nr_vtable;\nstatic void nr_init(void){}\nstatic void nr_deinit(void){}\n";
        let result = validate(src, "drv_nr", &[]);
        assert!(result.warnings.iter().any(|w| w.contains("missing read/write")));
    }

    // ── Bus-type detection ────────────────────────────────────────────────────

    #[test]
    fn detects_i2c_bus() {
        let src = minimal_source("i2c1"); // minimal_source uses pal_i2c_
        let result = validate(&src, "drv_i2c1", &[]);
        assert!(result.detected_bus_types.contains(&"i2c".to_string()));
    }

    #[test]
    fn detects_spi_bus() {
        let src = format!("{}\npal_spi_write(0, buf, 4);", minimal_source("spi1"));
        let result = validate(&src, "drv_spi1", &[]);
        assert!(result.detected_bus_types.contains(&"spi".to_string()));
    }

    #[test]
    fn detects_multiple_buses() {
        let src = format!(
            "{}\npal_spi_write(0, buf, 4);\npal_gpio_set(0, 1);",
            minimal_source("multi1")
        );
        let result = validate(&src, "drv_multi1", &[]);
        assert!(result.detected_bus_types.contains(&"i2c".to_string()));
        assert!(result.detected_bus_types.contains(&"spi".to_string()));
        assert!(result.detected_bus_types.contains(&"gpio".to_string()));
    }

    // ── Capability detection ──────────────────────────────────────────────────

    #[test]
    fn detects_cap_identifiers() {
        let src = minimal_source("cap1"); // has CAP_TEMPERATURE, CAP_HUMIDITY
        let result = validate(&src, "drv_cap1", &[]);
        assert!(result.detected_capabilities.contains(&"CAP_TEMPERATURE".to_string()));
        assert!(result.detected_capabilities.contains(&"CAP_HUMIDITY".to_string()));
    }

    #[test]
    fn deduplicates_capabilities() {
        let src = format!(
            "{}\n#define CAP_TEMPERATURE 99\nint x = CAP_TEMPERATURE;",
            minimal_source("cap2")
        );
        let result = validate(&src, "drv_cap2", &[]);
        let count = result
            .detected_capabilities
            .iter()
            .filter(|c| c.as_str() == "CAP_TEMPERATURE")
            .count();
        assert_eq!(count, 1, "CAP_TEMPERATURE should appear exactly once");
    }

    #[test]
    fn does_not_match_non_boundary_cap() {
        // "NOCAP_HEAT" should not be captured because the boundary check fails
        let src = format!("{}\nint NOCAP_HEAT = 1;", minimal_source("cap3"));
        let result = validate(&src, "drv_cap3", &[]);
        assert!(!result.detected_capabilities.contains(&"CAP_HEAT".to_string()));
    }

    // ── Vtable name extraction ────────────────────────────────────────────────

    #[test]
    fn extracts_vtable_name_plain() {
        let src = "driver_vtable_t drv_bmp390_vtable = {0};";
        assert_eq!(extract_vtable_name(src), Some("bmp390".to_string()));
    }

    #[test]
    fn extracts_vtable_name_const_prefix() {
        let src = "const driver_vtable_t drv_htu21_vtable;";
        // "const" appears before the type keyword so "driver_vtable_t" is still found
        // and "const" may appear after the keyword in alternative style.
        let src2 = "driver_vtable_t const drv_htu21_vtable;";
        assert_eq!(extract_vtable_name(src2), Some("htu21".to_string()));
        // First form: const comes before type; our scanner hits "driver_vtable_t drv_htu21"
        assert_eq!(extract_vtable_name(src), Some("htu21".to_string()));
    }

    #[test]
    fn returns_none_when_no_vtable() {
        assert_eq!(extract_vtable_name("int x = 0;"), None);
    }

    // ── source_lines count ────────────────────────────────────────────────────

    #[test]
    fn counts_source_lines() {
        let src = "line1\nline2\nline3\n";
        let result = validate(src, "drv_lc", &[]);
        assert_eq!(result.source_lines, 3);
    }
}
