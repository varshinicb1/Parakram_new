# Contributing to Parakram

Thank you for your interest in contributing to the future of zero-code hardware compilation!

## Architecture

Parakram has three tiers:
1. **Frontend** (`/playground`) — HTML/JS/CSS single-page application
2. **Backend** (`/backend`) — Rust (axum) server with Supabase PostgreSQL
3. **Firmware** (`/firmware`) — ESP-IDF C project for ESP32-S3

## Setup

### Backend
```bash
cd backend
cp .env.example .env   # fill in API keys
cargo run
```

### Firmware
```bash
cd firmware
export IDF_PATH=/path/to/esp-idf
. $IDF_PATH/export.sh
idf.py build
```

### Frontend
Open `http://localhost:8400` when the backend is running.

## Pull Request Guidelines

- Create a branch: `feat/your-feature` or `fix/your-bug`
- Ensure `cargo check` and `cargo fmt --check` pass
- Add tests for new endpoints
- Update documentation if adding new features
- Submit for review

## Reporting Issues

Use the in-app "Report Issue" button, or email `varshinicb@vidyuthlabs.co.in`.

## License

PolyForm Noncommercial 1.0.0 — see [LICENSE](../LICENSE) for details.
