# TradingChart — Architecture & Operations README 

# Live Deployment -> www.tradingchart.ink

> **Goal:** serve **financial price history** and **technical analysis tools** (OHLCV candles, indicators, time-series utilities) to a Next.js web app with low latency and high availability.

This document explains **how the whole system runs online**: topology, DNS/Routing, deployments, security posture, performance targets, observability, and day-2 operations. It complements the per-repo READMEs for **Backend (API)** and **Frontend (Next.js)**.

---

## System Overview

* **Frontend**: Next.js (app router) deployed as a **static site on Cloudflare Pages**. It renders charts (e.g., lightweight-charts) and calls the public API over HTTPS.
* **Backend (API)**: C++ service exposing **REST + WebSocket** for OHLCV data. Runs on **AWS EC2** (Amazon Linux 2023) in **sa-east-1 (São Paulo)**. Persists data in **DuckDB** on an attached volume. Ingests live candles from Binance WS, and (optionally) backfills via Binance REST.
* **Edge & DNS**: **Cloudflare** is the public edge (DNS + proxy).

  * `tradingchart.ink` → frontend (Pages)
  * `api.tradingchart.ink` → backend (EC2, proxied by Cloudflare)

The platform focuses on ingesting, persisting, and serving **historical market data (OHLCV)** while exposing primitives for **technical analysis** (interval selection, symbol metadata, and server-side indicator computation roadmap). The frontend consumes these datasets to render **interactive charts and TA overlays**.

---

## High-Level Topology

```mermaid
flowchart LR
    subgraph Client
      B[Browser]
    end

    subgraph Cloudflare
      CF_DNS[DNS & Proxy (WAF, TLS)]
      CF_Pages[Pages (Static Frontend)]
    end

    subgraph AWS sa-east-1
      EC2[EC2: API Container<br/>REST + WebSocket]
      VOL[(EBS/EFS: /data/market.duckdb)]
      Binance[(Binance WS/REST)]
    end

    B -- https://tradingchart.ink --> CF_Pages
    B -- https://api.tradingchart.ink --> CF_DNS --> EC2
    EC2 <-- live WS / backfill REST --> Binance
    EC2 <-- DuckDB file I/O --> VOL
```

---

## Domains & Routing

* **Apex**: `tradingchart.ink`
  Cloudflare Pages project hosts the static Next.js build.

  * Pages build command: `pnpm run build` (or `next build`)
  * Output directory: `out/` (via `output: 'export'` in `next.config.js`)

* **API**: `api.tradingchart.ink`
  Cloudflare DNS **CNAME** (proxied) → the EC2 public IP (or ALB DNS if using a load balancer).
  SSL/TLS terminates at Cloudflare (orange cloud). Health checks hit `/healthz`.

---

## Components

### Frontend (Next.js on Cloudflare Pages)

* Pure static export (`output: 'export'`) so Pages can serve it from the edge.
* Calls the API using `https://api.tradingchart.ink`.
* Avoids a Next.js `/api` layer to keep it static and fast.

**Build hints**

* `next.config.js`:

  ```js
  /** @type {import('next').NextConfig} */
  const nextConfig = {
    output: 'export',
    eslint: { ignoreDuringBuilds: true },
    typescript: { ignoreBuildErrors: true },
    images: { unoptimized: true },
  };
  export default nextConfig;
  ```
* `package.json` (relevant scripts):

  ```json
  {
    "scripts": {
      "build": "next build"
    }
  }
  ```

### Backend (C++ API on AWS EC2)

* **Endpoints**:

  * `GET /api/v1/symbols`
  * `GET /api/v1/intervals?symbol=...`
  * `GET /api/v1/candles?symbol=...&interval=...&limit=...`
  * `GET /healthz`, `GET /stats`, `GET /version`
  * **WebSocket**: `ws://<host>:<port>/ws` (via Cloudflare proxy → `wss://api.tradingchart.ink/ws`)
* **Data**: DuckDB file at `/data/market.duckdb`
* **Live ingestion**: Binance WS (kline 1m); backfill via Binance REST.
* **CORS**: Configurable via flags/env; Cloudflare adds extra perimeter controls.

---

## Environments

* **Production**

  * Region: **sa-east-1** (low latency to Latin America)
  * Instance: Amazon Linux 2023, Docker runtime
  * Security Group: inbound `80/tcp` from Cloudflare only (recommended), or 0.0.0.0/0 during bootstrap; outbound `443/tcp` for Binance.
  * Cloudflare: DNS proxied, WAF basic rules enabled.

* **Staging/Dev**

  * Optional separate EC2 or local Docker with CORS set to `*` or LAN origin.
  * Pages deploy preview branches as needed.

---

## Deployment — Backend (EC2)

1. **Build the image** (locally or on the instance)

   ```bash
   docker build -t tradingchart-api:local .
   ```

2. **Prepare storage**

   ```bash
   sudo mkdir -p /var/ttv/data
   sudo chown 1000:1000 /var/ttv/data
   sudo chmod u+rwX,g+rwX /var/ttv/data
   ```

3. **Run**

   ```bash
   sudo docker run -d --name tradingchart-api \
     -p 80:8080 \
     -v /var/ttv/data:/data \
     tradingchart-api:local \
     ./bin/api --live=1 --storage duck --duckdb /data/market.duckdb \
       --exchange binance \
       --live-symbols "BTCUSDT,ETHUSDT" \
       --live-intervals "1m" \
       --log-level info \
       --http.cors.enable=1 \
       --http.cors.origin "https://www.tradingchart.ink"
   ```

4. **Smoke tests (from your laptop)**

   ```bash
   curl -s https://api.tradingchart.ink/healthz
   curl -s "https://api.tradingchart.ink/api/v1/symbols"
   curl -s "https://api.tradingchart.ink/api/v1/candles?symbol=BTCUSDT&interval=1m&limit=5"
   curl -s "https://api.tradingchart.ink/stats"
   ```

> **Note**: If the API can’t write to DuckDB, check volume permissions and **run the container as the same UID/GID** that owns `/var/ttv/data` or loosen permissions during bootstrap.

---

## Deployment — Frontend (Cloudflare Pages)

* **Repo**: connect GitHub.
* **Build command**: `pnpm run build` (or `next build`)
* **Output dir**: `out/`
* **Environment variables (Pages)**:

  * `NEXT_PUBLIC_API_BASE=https://api.tradingchart.ink`

**DNS to Pages**

* Map `tradingchart.ink` (and `www`) to the Pages project in Cloudflare → Custom domain.

---

## CORS & WebSocket Origins

* **REST CORS**: controlled by flags/env:

  * `--http.cors.enable=1`
  * `--http.cors.origin "https://www.tradingchart.ink"` (prod)
  * For dev you can use `"*"` (without credentials) or a CSV once multi-origin support is added.
* **WebSocket**: browser WS handshake isn’t governed by HTTP CORS. Validate `Origin` upstream (Cloudflare WAF) and restrict who can reach the EC2 (Security Group and CF IPs).

---

## Security Posture

* **Perimeter**: Cloudflare (TLS, WAF, rate limiting if enabled)
* **Origin**: EC2 security group restricts inbound ports. Prefer allowing only Cloudflare IP ranges to port **80**.
* **Transport to Binance**: verify TLS (bundle CA in the image or rely on system CA).
* **Auth/Rate limiting**: not yet in the API; recommend enabling Cloudflare WAF rules / rate limits. Roadmap includes API keys/JWT if needed.
* **Data at rest**: DuckDB file on EBS/EFS; enable encrypted volumes.

---

## Performance & Scale

* **Targets**

  * REST latency p95 < 200 ms for `limit <= 600` (typical UI views).
  * WS: thousands of concurrent clients for final candle frames (small payloads).
  * Ingestion lag: ideally < 2 intervals (≤2 minutes at 1m).

* **Tuning knobs**

  * `--threads <n>` for HTTP concurrency.
  * Use **São Paulo (sa-east-1)** region to minimize RTT for LATAM.
  * Ensure `/data` is on general purpose SSD (gp3) or EFS with appropriate throughput if sharing.

* **Known hotspots**

  * JSON serialization for large `limit` fetches.
  * DuckDB reads when not cached.
  * WS backpressure (implementation hardening recommended).

---

## Observability

* **Health**: `GET /healthz` → `{"status":"ok"}` when live ingestion is healthy.
* **Stats**: `GET /stats` → uptime, reconnect counters, route latencies, `ws_state`, `last_msg_age_ms`.
* **Logging**: structured logs to stdout/stderr (Docker). Increase with `--log-level debug` during incidents.

**Suggested alerts**

* `ws_state == 0` for > 2m
* `last_msg_age_ms > 120000` (for 1m interval)
* Sudden growth in `rest_catchup_candles_total`

---

## Day-2 Operations

* **Rotate / Redeploy**

  ```bash
  sudo docker pull <image:tag> # if pushing to a registry
  sudo docker rm -f tradingchart-api
  sudo docker run ...          # same invocation as above
  ```

* **Backfill (optional)**

  * Run a one-off backfill container with `--backfill` flags (see backend README), mounting `/data` to persist the result.

* **Vacuum / maintenance**

  * Periodic DuckDB maintenance (VACUUM) if the DB grows due to re-ingestion or schema updates.

* **Cost watch**

  * Cloudflare: check **Analytics → Bandwidth/Requests**, enable **rate limiting** if needed.
  * AWS: **Billing & Cost Management → Cost Explorer** and **Budgets** alarms. EC2 size can be tuned down if idle.

---

## Local Development

* **API**

  ```bash
  ./bin/api --live=1 --storage duck --duckdb data/market.duckdb \
    --exchange binance \
    --live-symbols "BTCUSDT,ETHUSDT" \
    --live-intervals "1m" \
    --http.cors.enable=1 \
    --http.cors.origin="http://localhost:3000" \
    --log-level debug
  ```

* **Frontend**

  ```bash
  npm run dev
  # NEXT_PUBLIC_API_BASE=http://localhost:8080
  ```

---

## Troubleshooting Quick Reference

* **Frontend loads but “no chart data”**

  * `GET /api/v1/candles` returns `data: []` → likely DuckDB write permission or ingestion down.
  * Check `/stats` → `ws_state`, `last_msg_age_ms`.
  * On EC2: `docker logs -f tradingchart-api` for DuckDB permission errors.

* **522 from Cloudflare**

  * Origin not reachable: Security Group, container down, or port mismatch.
  * Test origin directly: `curl -I http://<EC2_PUBLIC_IP>/healthz`.

* **CORS errors in browser**

  * Ensure `--http.cors.enable=1` and `--http.cors.origin` matches the exact frontend origin.
  * Preflight (OPTIONS) support: if the browser sends preflight and API returns 404, add OPTIONS handling (roadmap item).

---

## Roadmap (Architecture)

* **P0**

  * Enforce WS backpressure; expose metrics for slow client closes.
  * Add `/readyz` endpoint (warmup vs ready).
  * TLS verification hardening for upstream providers.

* **P1**

  * API auth/rate limiting at the edge (Cloudflare) + optional API keys/JWT in the API.
  * Connection pooling / reduced per-request DuckDB open/close for high QPS.

* **P2**

  * Server-side indicator endpoints.
  * Multi-origin CORS (CSV + preflight OPTIONS) first-class support.
  * Official Dockerfile & CI to publish images.

---

## Quick URLs

* **Frontend**: `https://tradingchart.ink`
* **API**: `https://api.tradingchart.ink`

  * Health: `/healthz`
  * Stats: `/stats`
  * Symbols: `/api/v1/symbols`
  * Intervals: `/api/v1/intervals?symbol=BTCUSDT`
  * Candles: `/api/v1/candles?symbol=BTCUSDT&interval=1m&limit=600`
  * WebSocket: `wss://api.tradingchart.ink/ws`

---

**Contact/On-Call**

* EC2 access (SSH key), Cloudflare account, and GitHub repos are required for operations.
* Keep a runbook with the exact `docker run` line used in production, plus the current security group rules and Pages build settings.
