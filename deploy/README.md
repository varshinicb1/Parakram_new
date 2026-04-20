# Parakram — Cloud Deployment

Three deployment options, same container image.

## 1. Docker Compose (single VM)

Cheapest option — runs the full stack on one machine. Good up to ~1k req/s.

```bash
# On the server
git clone https://github.com/vidyuthlabs/parakram.git
cd parakram
cp backend/.env.example .env.production   # fill in real values
echo "DOMAIN=api.parakram.com" >> .env.production
echo "CERTBOT_EMAIL=ops@vidyuthlabs.co.in" >> .env.production

# First-time cert issuance
docker compose -f docker-compose.prod.yml run --rm certbot certonly \
    --webroot -w /var/www/certbot -d api.parakram.com

# Start the stack
docker compose -f docker-compose.prod.yml up -d
```

Stack:
- `backend` ×2 replicas (healthchecked, resource-limited)
- `nginx` (TLS termination, rate limiting, per-path routing)
- `certbot` (renews Let's Encrypt certs every 12h)
- `prometheus` (scrapes `/api/system/metrics`)

## 2. Azure Container Apps (recommended for scale)

Autoscaling 2→30 replicas, managed Log Analytics, per-request billing.

```bash
# One-time infra
az group create --name parakram-prod --location centralindia

# Store secrets in Key Vault (see secret.example.yaml for keys)
az keyvault create --name parakram-kv --resource-group parakram-prod

# Deploy
az deployment group create \
    --resource-group parakram-prod \
    --template-file deploy/azure/container_app.bicep \
    --parameters imageTag=sha-abc123 domain=api.parakram.com
```

Auto-deployed by `.github/workflows/deploy.yml` on main and on tags.

## 3. AWS ECS Fargate

For teams that prefer AWS. Uses Secrets Manager + ARM64 for cheaper runs.

```bash
aws ecs register-task-definition \
    --cli-input-json file://deploy/aws/ecs-task.json \
    --region ap-south-1

aws ecs create-service \
    --cluster parakram-prod \
    --service-name parakram-backend \
    --task-definition parakram-backend:latest \
    --desired-count 3 \
    --launch-type FARGATE
```

## 4. Kubernetes (any cluster)

Requires `kubectl`, `kustomize`, an NGINX ingress controller and cert-manager.

```bash
# Create secrets (NEVER commit real values)
kubectl create ns parakram
kubectl -n parakram create secret generic parakram-secrets \
    --from-env-file=.env.production

# Apply manifests
kubectl apply -k deploy/kubernetes/

# Check status
kubectl -n parakram get pods
kubectl -n parakram get ingress
```

Included:
- 3 replicas, rolling update (0 downtime)
- HPA 3→30 on CPU/memory
- Pod security context (non-root, read-only FS, dropped caps)
- Topology spread (different nodes when possible)
- Ingress with cert-manager TLS
- PodMonitor for Prometheus Operator

## Observability

Every deployment exposes:

| Endpoint | Purpose |
|---|---|
| `GET /api/system/health`  | Liveness — 200 if process is alive |
| `GET /api/system/ready`   | Readiness — 200 iff DB is reachable |
| `GET /api/system/metrics` | Prometheus text exposition |

Metrics exposed:
- `parakram_requests_total`, `parakram_requests_by_status{bucket="2xx|4xx|5xx"}`
- `parakram_llm_intents_total`
- `parakram_compiles_total`
- `parakram_deploys_total`
- `parakram_ros_graphs_total`
- `parakram_quota_rejections_total`
- `parakram_process_uptime_seconds`

## Required secrets

Set these in whichever secret store your deployment uses (KV / Secrets Manager / `.env.production` / k8s Secret):

- `SUPABASE_DB_URL`, `SUPABASE_JWT_SECRET`, `SUPABASE_URL`
- `ANTHROPIC_API_KEY`, `ANTHROPIC_MODEL`
- `STRIPE_SECRET_KEY`, `STRIPE_WEBHOOK_SECRET`
- `STRIPE_PRICE_HOBBY`, `STRIPE_PRICE_PRO`
