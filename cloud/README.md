# Cloud status backend — Cloudflare Worker + KV

Public, read-only online status page for the Kältekammer. The device POSTs its
status JSON here every 30 s; anyone with the URL sees live status. Free tier.

```
ESP32  --POST /ingest (Bearer token)-->  Worker  --put-->  KV ("latest")
Browser  --GET / (viewer) , GET /data-->  Worker  --get-->  KV
```

## One-time deploy

Prerequisites: a free Cloudflare account and Node. Install wrangler:
```bash
npm install -g wrangler
wrangler login
```

From this `cloud/` directory:
```bash
# 1. Create the KV namespace, then paste the printed id into wrangler.toml
wrangler kv namespace create STATUS_KV

# 2. Set the shared ingest secret (choose a long random string).
#    This MUST equal TELEMETRY_TOKEN in the firmware's config_secrets.h.
wrangler secret put INGEST_TOKEN

# 3. Deploy
wrangler deploy
```

`wrangler deploy` prints your public URL, e.g.
`https://kk-status.<your-subdomain>.workers.dev`.

## Point the firmware at it

In `src/config_secrets.h` (git-ignored):
```c
#define TELEMETRY_URL    "https://kk-status.<your-subdomain>.workers.dev/ingest"
#define TELEMETRY_TOKEN  "<the same secret you set with wrangler secret put>"
```
Rebuild + flash (or OTA). The device posts every `TELEMETRY_INTERVAL_MS` (30 s).

## URLs

| URL | Who | What |
|-----|-----|------|
| `/` | public | Auto-refreshing status viewer page |
| `/data` | public | Latest status JSON (`{received_at, status}`) |
| `/ingest` | device only (Bearer token) | POST endpoint for the ESP32 |

## Notes

- **Auth:** only `/ingest` is protected (bearer token). The viewer and `/data`
  are intentionally public read-only.
- **Privacy:** the viewer exposes temperatures, PV, mode, and alarms publicly.
  That is the intended design ("for developers and users to view").
- **Staleness:** the viewer flags the device as possibly offline if the last
  update is older than 120 s.
- **Cost:** one tiny KV write per device every 30 s — comfortably within the
  Cloudflare free tier.
- The firmware uses `setInsecure()` for the TLS connection (same rationale as
  OTA: avoids pinning a cert that breaks on rotation); the bearer token is what
  authenticates the device.
