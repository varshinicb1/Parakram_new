# Setup & Installation

## Prerequisites

- [Rust](https://rustup.rs/) (stable toolchain)
- A free [Supabase](https://supabase.com/) project (PostgreSQL database)
- An [OpenRouter](https://openrouter.ai/) API key (for LLM compilation)

## Quick Start

### 1. Clone the Repository
```bash
git clone https://github.com/YourOrg/Parakram.git
cd Parakram
```

### 2. Configure Environment
```bash
cd backend
cp .env.example .env
```

Edit `.env` with your credentials:
```env
SUPABASE_DB_URL=postgres://postgres:yourpassword@db.yourproject.supabase.co:5432/postgres
SUPABASE_URL=https://yourproject.supabase.co
JWT_SECRET=your-supabase-jwt-secret
OPENROUTER_API_KEY=sk-or-v1-your-key
STRIPE_SECRET_KEY=sk_test_your-stripe-key
STRIPE_PRICE_MAKER=price_your-stripe-price-id
SENDGRID_API_KEY=SG.your-sendgrid-key
```

### 3. Run the Backend
```bash
cargo run
```
The server starts on `http://localhost:8400`.

### 4. Open the Playground
Navigate to `http://localhost:8400` in Chrome or Edge. The playground UI loads automatically.

## Pricing

| Plan | Price | Projects | Devices | LLM Intents/mo |
|------|-------|----------|---------|-----------------|
| Free | $0 | 2 | 2 | 50 |
| Maker | $1.50/mo | Unlimited | 10 | 500 |
