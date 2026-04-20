// Parakram — Azure Container Apps deployment.
// Deploy:
//   az deployment group create \
//     --resource-group parakram-prod \
//     --template-file container_app.bicep \
//     --parameters imageTag=<sha> domain=api.parakram.com

@description('Deployment environment: dev | staging | prod')
param env string = 'prod'

@description('Docker image tag (typically the git SHA)')
param imageTag string = 'latest'

@description('Fully qualified domain for the app (used by ingress)')
param domain string = 'api.parakram.com'

@description('Azure region — default: Central India (closest to Vidyuthlabs HQ)')
param location string = resourceGroup().location

var appName      = 'parakram-backend-${env}'
var envName      = 'parakram-env-${env}'
var registryName = 'ghcr.io/vidyuthlabs'

// ── Log Analytics workspace (backs Container Apps Environment) ─────────────
resource logs 'Microsoft.OperationalInsights/workspaces@2022-10-01' = {
  name: 'parakram-logs-${env}'
  location: location
  properties: {
    sku: { name: 'PerGB2018' }
    retentionInDays: 30
  }
}

// ── Container Apps Environment ─────────────────────────────────────────────
resource cae 'Microsoft.App/managedEnvironments@2024-03-01' = {
  name: envName
  location: location
  properties: {
    appLogsConfiguration: {
      destination: 'log-analytics'
      logAnalyticsConfiguration: {
        customerId: logs.properties.customerId
        sharedKey: logs.listKeys().primarySharedKey
      }
    }
    workloadProfiles: [
      { name: 'Consumption', workloadProfileType: 'Consumption' }
    ]
  }
}

// ── The app itself ─────────────────────────────────────────────────────────
resource app 'Microsoft.App/containerApps@2024-03-01' = {
  name: appName
  location: location
  properties: {
    managedEnvironmentId: cae.id
    workloadProfileName: 'Consumption'
    configuration: {
      ingress: {
        external: true
        targetPort: 8400
        transport: 'http'
        customDomains: [
          { name: domain, bindingType: 'SniEnabled' }
        ]
      }
      // Secrets must be created via az CLI (not hardcoded).
      secrets: [
        { name: 'supabase-db-url',      value: 'fetch-from-kv' }
        { name: 'supabase-jwt-secret',  value: 'fetch-from-kv' }
        { name: 'anthropic-api-key',    value: 'fetch-from-kv' }
        { name: 'stripe-secret-key',    value: 'fetch-from-kv' }
        { name: 'stripe-webhook-secret',value: 'fetch-from-kv' }
      ]
    }
    template: {
      containers: [
        {
          name: 'backend'
          image: '${registryName}/parakram-backend:${imageTag}'
          resources: { cpu: json('0.5'), memory: '1Gi' }
          env: [
            { name: 'SUPABASE_DB_URL',       secretRef: 'supabase-db-url' }
            { name: 'SUPABASE_JWT_SECRET',   secretRef: 'supabase-jwt-secret' }
            { name: 'ANTHROPIC_API_KEY',     secretRef: 'anthropic-api-key' }
            { name: 'STRIPE_SECRET_KEY',     secretRef: 'stripe-secret-key' }
            { name: 'STRIPE_WEBHOOK_SECRET', secretRef: 'stripe-webhook-secret' }
            { name: 'BIND_ADDR', value: '0.0.0.0:8400' }
            { name: 'RUST_LOG',  value: 'parakram_backend=info,tower_http=info' }
          ]
          probes: [
            {
              type: 'Liveness'
              httpGet: { path: '/api/system/health', port: 8400 }
              periodSeconds: 30
            }
            {
              type: 'Readiness'
              httpGet: { path: '/api/system/ready', port: 8400 }
              periodSeconds: 10
            }
          ]
        }
      ]
      scale: {
        minReplicas: 2
        maxReplicas: 30
        rules: [
          {
            name: 'http-scale'
            http: { metadata: { concurrentRequests: '100' } }
          }
        ]
      }
    }
  }
}

output fqdn string = app.properties.configuration.ingress.fqdn
