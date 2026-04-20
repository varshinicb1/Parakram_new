//! Minimal Prometheus-format metrics — zero dependencies.
//!
//! Tracks request counts, LLM-intent / compile / deploy counters, and
//! process uptime. Emitted via GET /api/system/metrics.
//!
//! Kept intentionally small to avoid pulling `prometheus` / `metrics` crates.

use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::LazyLock;
use std::time::Instant;

pub struct Counter(AtomicU64);

impl Counter {
    const fn new() -> Self { Self(AtomicU64::new(0)) }
    pub fn inc(&self) { self.0.fetch_add(1, Ordering::Relaxed); }
    pub fn add(&self, n: u64) { self.0.fetch_add(n, Ordering::Relaxed); }
    pub fn get(&self) -> u64 { self.0.load(Ordering::Relaxed) }
}

pub static REQUESTS_TOTAL:        Counter = Counter::new();
pub static REQUESTS_2XX:          Counter = Counter::new();
pub static REQUESTS_4XX:          Counter = Counter::new();
pub static REQUESTS_5XX:          Counter = Counter::new();
pub static LLM_INTENTS_TOTAL:     Counter = Counter::new();
pub static COMPILES_TOTAL:        Counter = Counter::new();
pub static DEPLOYS_TOTAL:         Counter = Counter::new();
pub static ROS_GRAPHS_TOTAL:      Counter = Counter::new();
pub static QUOTA_REJECTIONS:      Counter = Counter::new();

static PROCESS_START: LazyLock<Instant> = LazyLock::new(Instant::now);

/// Emit metrics in Prometheus text exposition format 0.0.4.
pub fn render() -> String {
    let uptime = PROCESS_START.elapsed().as_secs_f64();
    format!(
        "# HELP parakram_requests_total Total HTTP requests received\n\
         # TYPE parakram_requests_total counter\n\
         parakram_requests_total {}\n\
         # HELP parakram_requests_by_status HTTP requests by 2xx/4xx/5xx bucket\n\
         # TYPE parakram_requests_by_status counter\n\
         parakram_requests_by_status{{bucket=\"2xx\"}} {}\n\
         parakram_requests_by_status{{bucket=\"4xx\"}} {}\n\
         parakram_requests_by_status{{bucket=\"5xx\"}} {}\n\
         # HELP parakram_llm_intents_total LLM intent requests that produced IR\n\
         # TYPE parakram_llm_intents_total counter\n\
         parakram_llm_intents_total {}\n\
         # HELP parakram_compiles_total Bytecode compilations completed\n\
         # TYPE parakram_compiles_total counter\n\
         parakram_compiles_total {}\n\
         # HELP parakram_deploys_total Deployments to devices completed\n\
         # TYPE parakram_deploys_total counter\n\
         parakram_deploys_total {}\n\
         # HELP parakram_ros_graphs_total ROS 2 node graphs generated\n\
         # TYPE parakram_ros_graphs_total counter\n\
         parakram_ros_graphs_total {}\n\
         # HELP parakram_quota_rejections_total Requests rejected due to plan quota\n\
         # TYPE parakram_quota_rejections_total counter\n\
         parakram_quota_rejections_total {}\n\
         # HELP parakram_process_uptime_seconds Seconds since backend start\n\
         # TYPE parakram_process_uptime_seconds gauge\n\
         parakram_process_uptime_seconds {:.3}\n",
        REQUESTS_TOTAL.get(),
        REQUESTS_2XX.get(), REQUESTS_4XX.get(), REQUESTS_5XX.get(),
        LLM_INTENTS_TOTAL.get(),
        COMPILES_TOTAL.get(),
        DEPLOYS_TOTAL.get(),
        ROS_GRAPHS_TOTAL.get(),
        QUOTA_REJECTIONS.get(),
        uptime,
    )
}
