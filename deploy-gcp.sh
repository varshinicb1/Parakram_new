#!/usr/bin/env bash
# Parakram — one-shot Google Cloud Run deployment
# Run this from Cloud Shell after cloning the repo.
# Usage: bash deploy-gcp.sh
set -euo pipefail

# ─── CONFIG — fill these in ──────────────────────────────────────────────────
PROJECT_ID="${GCP_PROJECT_ID:-}"                  # your GCP project ID
REGION="asia-south1"                              # Mumbai — closest to India
SERVICE="parakram-backend"
REPO="parakram"
IMAGE="${REGION}-docker.pkg.dev/${PROJECT_ID}/${REPO}/${SERVICE}"

# Secrets (plain text — will be stored in Secret Manager, NOT in env)
SUPABASE_DB_URL="${SUPABASE_DB_URL:-}"            # postgresql://postgres.xxx:PASSWORD@...
SUPABASE_JWT_SECRET="${SUPABASE_JWT_SECRET:-}"    # from Supabase → Settings → API → JWT Secret
JWT_SECRET="${JWT_SECRET:-}"                      # any 32+ char random string
OPENROUTER_API_KEY="${OPENROUTER_API_KEY:-}"      # sk-or-v1-...
STRIPE_SECRET_KEY="${STRIPE_SECRET_KEY:-sk_test_placeholder}"
STRIPE_WEBHOOK_SECRET="${STRIPE_WEBHOOK_SECRET:-whsec_placeholder}"
SENDGRID_API_KEY="${SENDGRID_API_KEY:-}"          # optional — emails log to console if unset

# ─── VALIDATION ──────────────────────────────────────────────────────────────
check_required() {
    local missing=0
    for var in PROJECT_ID SUPABASE_DB_URL SUPABASE_JWT_SECRET JWT_SECRET OPENROUTER_API_KEY; do
        if [[ -z "${!var}" ]]; then
            echo "ERROR: $var is required but not set"
            missing=1
        fi
    done
    [[ $missing -eq 1 ]] && exit 1
}
check_required

echo "==> Deploying Parakram backend to Cloud Run"
echo "    Project:  $PROJECT_ID"
echo "    Region:   $REGION"
echo "    Image:    $IMAGE"
echo ""

# ─── 1. Set project ──────────────────────────────────────────────────────────
gcloud config set project "$PROJECT_ID"

# ─── 2. Enable required APIs ─────────────────────────────────────────────────
echo "==> Enabling APIs..."
gcloud services enable \
    run.googleapis.com \
    artifactregistry.googleapis.com \
    cloudbuild.googleapis.com \
    secretmanager.googleapis.com \
    --quiet

# ─── 3. Create Artifact Registry repo (idempotent) ───────────────────────────
echo "==> Creating Artifact Registry repo..."
gcloud artifacts repositories create "$REPO" \
    --repository-format=docker \
    --location="$REGION" \
    --description="Parakram backend images" \
    --quiet 2>/dev/null || echo "    (repo already exists, skipping)"

# ─── 4. Configure Docker auth ────────────────────────────────────────────────
gcloud auth configure-docker "${REGION}-docker.pkg.dev" --quiet

# ─── 5. Build & push image ───────────────────────────────────────────────────
echo "==> Building Docker image (this takes 3-5 min first time)..."
cd backend
docker build -t "${IMAGE}:latest" .
docker push "${IMAGE}:latest"
cd ..

# ─── 6. Store secrets in Secret Manager ─────────────────────────────────────
echo "==> Storing secrets in Secret Manager..."
store_secret() {
    local name="$1" value="$2"
    if gcloud secrets describe "$name" --quiet 2>/dev/null; then
        echo "$value" | gcloud secrets versions add "$name" --data-file=-
    else
        echo "$value" | gcloud secrets create "$name" --data-file=- --replication-policy=automatic
    fi
    echo "    ✓ $name"
}

store_secret "supabase-db-url"          "$SUPABASE_DB_URL"
store_secret "supabase-jwt-secret"      "$SUPABASE_JWT_SECRET"
store_secret "jwt-secret"               "$JWT_SECRET"
store_secret "openrouter-api-key"       "$OPENROUTER_API_KEY"
store_secret "stripe-secret-key"        "$STRIPE_SECRET_KEY"
store_secret "stripe-webhook-secret"    "$STRIPE_WEBHOOK_SECRET"
[[ -n "$SENDGRID_API_KEY" ]] && store_secret "sendgrid-api-key" "$SENDGRID_API_KEY"

# Grant Cloud Run SA access to secrets
SA="${PROJECT_ID}@appspot.gserviceaccount.com"
for secret in supabase-db-url supabase-jwt-secret jwt-secret openrouter-api-key \
              stripe-secret-key stripe-webhook-secret; do
    gcloud secrets add-iam-policy-binding "$secret" \
        --member="serviceAccount:${SA}" \
        --role="roles/secretmanager.secretAccessor" \
        --quiet 2>/dev/null || true
done

# ─── 7. Deploy to Cloud Run ──────────────────────────────────────────────────
echo "==> Deploying to Cloud Run..."

SECRET_FLAGS=(
    "SUPABASE_DB_URL=supabase-db-url:latest"
    "SUPABASE_JWT_SECRET=supabase-jwt-secret:latest"
    "JWT_SECRET=jwt-secret:latest"
    "OPENROUTER_API_KEY=openrouter-api-key:latest"
    "STRIPE_SECRET_KEY=stripe-secret-key:latest"
    "STRIPE_WEBHOOK_SECRET=stripe-webhook-secret:latest"
)
[[ -n "$SENDGRID_API_KEY" ]] && SECRET_FLAGS+=("SENDGRID_API_KEY=sendgrid-api-key:latest")

SECRETS_ARG=$(IFS=,; echo "${SECRET_FLAGS[*]}")

gcloud run deploy "$SERVICE" \
    --image="${IMAGE}:latest" \
    --region="$REGION" \
    --platform=managed \
    --allow-unauthenticated \
    --port=8400 \
    --min-instances=0 \
    --max-instances=10 \
    --memory=512Mi \
    --cpu=1 \
    --concurrency=80 \
    --timeout=60s \
    --set-secrets="$SECRETS_ARG" \
    --set-env-vars="RUST_LOG=parakram_backend=info${SUPABASE_URL:+",SUPABASE_URL=$SUPABASE_URL"}" \
    --quiet

# ─── 8. Print service URL ────────────────────────────────────────────────────
URL=$(gcloud run services describe "$SERVICE" --region="$REGION" --format='value(status.url)')
echo ""
echo "=========================================================="
echo "  Parakram backend deployed!"
echo "  URL: $URL"
echo "  Health: $URL/api/system/health"
echo "=========================================================="
echo ""
echo "Next steps:"
echo "  1. Update your Android/iOS app's backend URL to: $URL"
echo "  2. Configure Stripe webhook endpoint: $URL/api/billing/webhook"
echo "  3. Add domain via: gcloud run domain-mappings create --service=$SERVICE --domain=api.parakram.io --region=$REGION"
