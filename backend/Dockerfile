# syntax=docker/dockerfile:1.7
# Parakram Backend — production image
# Multi-stage build with dependency caching + distroless-style runtime.

# ── Stage 1: cargo-chef plan ───────────────────────────────────────────────
FROM rust:1.88-slim-bookworm AS chef
WORKDIR /app
RUN cargo install cargo-chef --locked --version 0.1.67 \
    && rm -rf $CARGO_HOME/registry

FROM chef AS planner
COPY Cargo.toml Cargo.lock ./
COPY src ./src
RUN cargo chef prepare --recipe-path recipe.json

# ── Stage 2: cached dependency build ──────────────────────────────────────
FROM chef AS builder
RUN apt-get update \
    && apt-get install -y --no-install-recommends pkg-config libssl-dev \
    && rm -rf /var/lib/apt/lists/*

COPY --from=planner /app/recipe.json recipe.json
RUN cargo chef cook --release --recipe-path recipe.json

# Actual source build (fast: deps are already compiled)
COPY Cargo.toml Cargo.lock ./
COPY src ./src
RUN cargo build --release --bin parakram-backend \
    && strip target/release/parakram-backend

# ── Stage 3: minimal runtime ──────────────────────────────────────────────
FROM debian:bookworm-slim AS runtime
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates libssl3 tini curl \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --system --gid 1001 parakram \
    && useradd --system --uid 1001 --gid parakram --home /app --shell /sbin/nologin parakram \
    && mkdir -p /app/data \
    && chown -R parakram:parakram /app

WORKDIR /app
COPY --from=builder --chown=parakram:parakram /app/target/release/parakram-backend /app/parakram-backend

USER parakram
EXPOSE 8400

ENV RUST_LOG=parakram_backend=info,tower_http=info \
    BIND_ADDR=0.0.0.0:8400

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD curl --fail --silent http://localhost:8400/api/system/health || exit 1

# tini as PID 1 so SIGTERM reaches the app cleanly
ENTRYPOINT ["/usr/bin/tini", "--"]
CMD ["/app/parakram-backend"]
