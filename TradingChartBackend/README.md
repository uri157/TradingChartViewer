# The Trading Viewer API Backend


## Table of Contents
- [1. Project Overview](#1-project-overview)
- [2. Architecture](#2-architecture)
- [3. REST Endpoints](#3-rest-endpoints)
- [4. CORS and Access Policies](#4-cors-and-access-policies)
- [5. Environment Variables and Flags](#5-environment-variables-and-flags)
- [6. Local Execution with Docker](#6-local-execution-with-docker)
- [7. Docker Compose](#7-docker-compose)
- [8. AWS EC2 Deployment](#8-aws-ec2-deployment)
- [9. Observability and Metrics](#9-observability-and-metrics)
- [10. Performance and Concurrency](#10-performance-and-concurrency)
- [11. Security](#11-security)
- [12. Testing and QA](#12-testing-and-qa)
- [13. Troubleshooting (FAQ)](#13-troubleshooting-faq)
- [14. Technical Roadmap](#14-technical-roadmap)
- [15. License and Credits](#15-license-and-credits)

## 1. Project Overview

**Trading Viewer API** is a C++17/20 backend service that exposes OHLCV market data via HTTP and WebSocket. It runs a custom HTTP server (`HttpServer`) and manages live Binance streams through REST catch-up plus WebSocket streaming. Closed candles are persisted in DuckDB for consistent, fast queries.

**Core technologies:** C++17/20, DuckDB, custom HTTP/WebSocket server, Boost, Docker, Binance ingestion.

**Deployment text diagram:**

```
Client → Cloudflare → EC2:80 → container (8080) → HttpServer → Repository (DuckDB)
```

The main binary (`./bin/api`) boots the server, applies DuckDB migrations, and coordinates the live ingestion pipeline (`LiveIngestor`) when live mode is enabled.

## 2. Architecture

### 2.1 Folder tree (summary)

```
├── Dockerfile               # Multi-stage image (builder + runtime) with the api binary ready
├── Makefile                 # Build targets (api/main), DuckDB vendored integration
├── docker-compose*.yml      # Base orchestration + dev/prod overlays
├── scripts/                 # Utilities (e.g. probe_http.sh for smoke tests)
├── src/
│   ├── api/                 # HttpServer, Router, WebSocketServer, REST controllers
│   ├── adapters/            # External integrations (DuckDB, Binance REST/WS, legacy)
│   ├── app/                 # Domain services (LiveIngestor, BackfillWorker)
│   ├── common/              # Configuration, logging, shared utilities
│   ├── domain/              # Market data models and contracts
│   ├── metrics/             # In-memory metrics registry (gauges, counters)
│   └── ...                  # geo/, indicators/, infra/, tools/, _legacy/
├── tests/                   # (TODO: document/enable)
├── vendor/ / third_party/   # Vendored dependencies (including DuckDB)
└── web/                     # Frontend (out of scope for this README)
```

### 2.2 Data flow

1. **Initial ingest (REST catch-up):** `LiveIngestor` detects gaps in DuckDB, requests history over REST, and uses `DuckCandleRepo::upsert_batch` to insert in batches.
2. **Live streaming:** `LiveIngestor` subscribes via `BinanceWsClient`, processes events, and publishes closed candles (optionally partials).
3. **Persistence:** Candles are stored in DuckDB (`DuckCandleRepo`) with `INSERT OR REPLACE`, ensuring idempotency by (symbol, interval, ts).
4. **Exposure:** `HttpServer` handles HTTP and WebSocket traffic. `Router` maps GET requests to controllers (`/healthz`, `/api/v1/*`, `/stats`).

### 2.3 Key components

- **`HttpServer`:** Minimal HTTP server with request line parser, essential header parsing, optional CORS writer, and delegation to the `Router`. Shares the listening socket with the WebSocket upgrader.
- **`Router`:** GET route table (`/healthz`, `/version`, `/stats`, `/api/v1/symbols`, `/api/v1/intervals`, `/api/v1/candles`). Provides basic in-memory caching for idempotent responses and nested routes (`/api/v1/symbols/:symbol/intervals`).
- **DuckDB repository:** `DuckStore` applies initial migrations; `DuckCandleRepo` implements `getCandles`, `listSymbols`, `upsert_batch`, and range queries. Limitations: requires RW filesystem, batch transactions, no automatic compaction.
- **`LiveIngestor`:** Orchestrates REST resync (bootstrap) and continuous WebSocket listening. Validates intervals, throttles partials (`WS_EMIT_PARTIALS`, `WS_PARTIAL_THROTTLE_MS`), and updates gauges (`ws_state`, `last_msg_age_ms`) surfaced by `/stats`.

### 2.4 Technical decisions

- **DuckDB** offers embedded columnar analytics, full SQL, and ACID semantics in a single file—ideal for OHLCV snapshots.
- **WebSocket** handles real-time candles and fast updates, with backpressure control (queue `max_msgs`, `max_bytes`, `stall_timeout`).
- **Backpressure and limits:** The API enforces `default_limit=600` and `max_limit=5000` for `/candles`, protecting clients and the DB. WebSocket sessions close when configured limits are exceeded.


- **`HttpServer`:** Servidor HTTP minimalista con parser de request line, lectura de headers esenciales, soporte CORS opcional y delegación al `Router`. Comparte socket listening con el WebSocket upgrader.
- **`Router`:** Tabla de rutas GET (`/healthz`, `/version`, `/stats`, `/api/v1/symbols`, `/api/v1/intervals`, `/api/v1/candles`). Gestiona cacheo básico en memoria para respuestas GET idempotentes y rutas anidadas (`/api/v1/symbols/:symbol/intervals`).
- **Repositorio DuckDB:** `DuckStore` aplica migraciones iniciales, `DuckCandleRepo` implementa `getCandles`, `listSymbols`, `upsert_batch` y consultas auxiliares (rangos min/max). Limitaciones: requiere filesystem RW, transacciones por lote, sin compacción automática.
- **`LiveIngestor`:** Orquesta resync REST (bootstrap) y escucha WebSocket continuo. Valida intervalos, gestiona throttling de parciales (`WS_EMIT_PARTIALS`, `WS_PARTIAL_THROTTLE_MS`), mantiene gauges (`ws_state`, `last_msg_age_ms`) para `/stats`.


Endpoints live at `http://<host>:<port>`. Responses use UTF-8 JSON.

| Method | Path | Description |
| --- | --- | --- |
| GET | `/healthz` | Simple liveness check. |
| GET | `/version` | Version information (TODO: define payload). |
| GET | `/stats` | Runtime metrics. |
| GET | `/api/v1/symbols` | Lists known symbols and status (`active` if live subscription is active). |
| GET | `/api/v1/intervals` | Supported intervals. Requires `symbol`. |
| GET | `/api/v1/candles` | Returns OHLCV data by symbol/interval, ascending order. |

### 3.1 Examples

`GET /healthz`

```json
{"status":"ok"}
```

`GET /api/v1/symbols`

```json
{
  "symbols": [
    {"symbol":"BTCUSDT","base":"BTC","quote":"USDT","status":"active"},
    {"symbol":"ETHUSDT","base":"ETH","quote":"USDT","status":"active"}
  ]
}
```

`GET /api/v1/intervals?symbol=BTCUSDT`

```json
{
  "symbol": "BTCUSDT",
  "intervals": ["1m", "5m", "1h", "1d"]
}
```

`GET /api/v1/candles?symbol=BTCUSDT&interval=1m&limit=5`

```json
{
  "symbol": "BTCUSDT",
  "interval": "1m",
  "data": [
    [1726235400000, 63715.1, 63725.0, 63705.5, 63720.2, 18.42],
    [1726235460000, 63720.2, 63740.7, 63718.3, 63735.4, 22.01],
    [1726235520000, 63735.4, 63760.0, 63730.0, 63755.5, 31.08],
    [1726235580000, 63755.5, 63780.4, 63748.2, 63770.1, 28.76],
    [1726235640000, 63770.1, 63790.0, 63760.5, 63785.0, 25.63]
  ]
}
```

`GET /stats`

```json
{
  "uptime_seconds": 86400.42,
  "threads": 4,
  "backend_active": true,
  "reconnect_attempts_total": 2,
  "rest_catchup_candles_total": 2500,
  "ws_state": 1,
  "last_msg_age_ms": 520.5,
  "routes": {
    "GET /api/v1/candles": {"requests": 123456, "p95_ms": 4.8, "p99_ms": 7.3}
  }
}
```


### 3.2 Error codes and limits

- `400`: invalid parameters (`symbol`, `interval`, `from/to`, `limit`).
- `404`: unregistered routes; also happens for `OPTIONS` (preflight) when no handler exists.
- `429`: **TODO:** throttling not implemented (use external proxy).
- `500`: internal failures (DuckDB, parsing, upstream Binance).
- Upstream `5xx`: `522` from Cloudflare when EC2 does not respond (check SG, container).

**`/api/v1/candles` limits:** default `limit=600`, maximum `5000`. For historical pagination use `from/to` and timestamp-based paging; respect `max_limit` to avoid empty responses.

## 4. CORS and Access Policies

- When `--http.cors.enable=1` and `--http.cors.origin` is set, `HttpServer` writes `Access-Control-Allow-Origin`, `Vary: Origin`, and `Access-Control-Allow-Headers: Content-Type` on each response.
- Preflight `OPTIONS` is not handled automatically; without a handler the server returns `404`.
- The allowed origin is literal (no dynamic wildcard). For multiple origins rely on a proxy or patch the server (see Roadmap).
- WebSockets ignore the CORS flags.

### 4.1 LAN development

Allow requests from `http://localhost:3000` by running the container with:

```
--http.cors.enable=1 \
--http.cors.origin "http://localhost:3000"
```

For quick tests without credentials you may use `"*"`, **only in development environments**.

### 4.2 Production

Configure a closed origin:


```
--http.cors.origin "https://www.tradingchart.ink"
```

If you also serve from `https://tradingchart.ink`, duplicate the deployment or use a proxy that rewrites `Origin`.

### 4.3 Verification with curl

Preflight (will return 404 until a handler is implemented; useful to validate current behavior):


```bash
curl -i -X OPTIONS \
  -H "Origin: http://localhost:3000" \
  -H "Access-Control-Request-Method: GET" \
  http://localhost:8080/api/v1/candles
```


Simple request:


```bash
curl -i -H "Origin: http://localhost:3000" \
  "http://localhost:8080/api/v1/candles?symbol=BTCUSDT&interval=1m&limit=5"
```

## 5. Environment Variables and Flags

CLI flags take precedence over environment variables. Flags follow the `--key value` or `--key=value` format.

| NAME | Type | Default | Example | Description |
| --- | --- | --- | --- | --- |
| `LIVE` (flag `--live`) | bool | `0` | `--live=1` | Enables live ingestion (requires DuckDB and live symbols/intervals). |
| `STORAGE` (flag `--storage`) | `duck \| legacy` | `legacy` | `--storage duck` | Selects the candle backend. Live ingestion requires `duck`. |
| `DUCKDB` (flag `--duckdb`) | path | `data/market.duckdb` | `--duckdb /data/market.duckdb` | DuckDB file path. Creates parent directories if missing. |
| `EXCHANGE` (flag `--exchange`) | text | `binance` | `--exchange binance` | Upstream used for backfill/live. Currently Binance only. |
| `LIVE_SYMBOLS` (flag `--live-symbols`) | CSV | _required in live_ | `--live-symbols "BTCUSDT,ETHUSDT"` | List of symbols subscribed to the stream. |
| `LIVE_INTERVALS` (flag `--live-intervals`) | CSV | _required in live_ | `--live-intervals "1m"` | Must currently contain exactly `1m`. |
| `LOG_LEVEL` (`env` + flag) | `debug\|info\|warn\|error` | `info` | `LOG_LEVEL=debug` | Controls logging verbosity. |
| `HTTP_CORS_ENABLE` (flag `--http.cors.enable`) | `0\|1` | `0` | `--http.cors.enable=1` | Enables CORS headers. |
| `HTTP_CORS_ORIGIN` (flag `--http.cors.origin`) | text | empty | `--http.cors.origin "https://www.tradingchart.ink"` | Literal allowed origin. |
| `PORT` (env / flag `--port`) | uint16 | `8080` | `PORT=8080` | HTTP/WS port. |
| `THREADS` (flag `--threads`) | integer ≥1 | `1` | `--threads 4` | HTTP workers. |
| `HTTP_DEFAULT_LIMIT` (env/flag) | integer | `600` | `--http-default-limit 1000` | Default `/candles` limit. |
| `HTTP_MAX_LIMIT` (env/flag) | integer | `5000` | `--http-max-limit 10000` | Maximum `/candles` limit. |
| `WS_PING_PERIOD_MS` (env/flag) | ms | `30000` | `--ws-ping-period-ms 45000` | WS ping keepalive period. |
| `WS_PONG_TIMEOUT_MS` (env/flag) | ms | `75000` | `--ws-pong-timeout-ms 90000` | Pong timeout. |
| `WS_SEND_QUEUE_MAX_MSGS` (env/flag) | integer | `500` | `--ws-send-queue-max-msgs 300` | Max queued WS messages before closing. |
| `WS_SEND_QUEUE_MAX_BYTES` (env/flag) | bytes | `15728640` | `--ws-send-queue-max-bytes 8000000` | Max queued WS bytes. |
| `WS_STALL_TIMEOUT_MS` (env/flag) | ms | `20000` | `--ws-stall-timeout-ms 30000` | Max wait while freeing the queue. |
| `WS_EMIT_PARTIALS` (env) | bool | `true` | `WS_EMIT_PARTIALS=false` | Emit partial candles via WebSocket. |
| `WS_PARTIAL_THROTTLE_MS` (env) | ms | `0` | `WS_PARTIAL_THROTTLE_MS=500` | Minimum delay between partials of the same candle. |

> **Note:** Pure environment variables (`LIVE`, `STORAGE`, etc.) are not implemented; configure them through flags. Use wrappers (Make/Compose) to inject them. Adding direct environment support remains a TODO.

### 5.1 Sample `.env` (development)

```env
# .env.dev
LOG_LEVEL=debug
CORS_ENABLE=1
CORS_ORIGIN=http://localhost:3000
PORT=8080
HTTP_DEFAULT_LIMIT=600
HTTP_MAX_LIMIT=5000
```

### 5.2 Sample `.env` (production)

```env
# .env.prod
LOG_LEVEL=info
CORS_ENABLE=1
CORS_ORIGIN=https://www.tradingchart.ink
PORT=8080
HTTP_DEFAULT_LIMIT=600
HTTP_MAX_LIMIT=5000
```


## 6. Local Execution with Docker

### 6.1 Requirements

- Docker 24+
- Docker Compose v2 (optional, see section 7)
- Disk space for DuckDB (`./data` folder)

### 6.2 Manual build and run


```bash
docker build -t tradingchart-api:local .
mkdir -p ./data
docker run -d --name tradingchart-api \
  -p 8080:8080 \
  -v $(pwd)/data:/data \
  tradingchart-api:local \
  ./bin/api --live=1 --storage duck --duckdb /data/market.duckdb \
    --exchange binance \
    --live-symbols "BTCUSDT,ETHUSDT" \
    --live-intervals "1m" \
    --log-level info \
    --http.cors.enable=1 \
    --http.cors.origin "http://localhost:3000"
```

### 6.3 Smoke tests

```bash
curl -sf http://localhost:8080/healthz
curl -sf "http://localhost:8080/api/v1/symbols"
curl -sf "http://localhost:8080/api/v1/candles?symbol=BTCUSDT&interval=1m&limit=5"
```


For a more complete check use `scripts/probe_http.sh` (requires `bash`, `curl`).

### 6.4 DuckDB permissions

The `/data` volume must be writable by the process UID inside the container (`app` ≈ UID/GID 1000). If you see `Permission denied`:

1. Fix host permissions: `sudo chown -R 1000:1000 ./data`.
2. Or run the container with `--user 1000:1000`.
3. In the dev Compose file the service already uses `user: "${UID:-1000}:${GID:-1000}"`.

## 7. Docker Compose

The base file `docker-compose.yml` defines the image, command with default flags, and healthcheck. Overlays add storage semantics per environment:

- **`docker-compose.dev.yml`:** mounts `./data` and runs as your UID/GID, reads `.env.dev`.
- **`docker-compose.prod.yml`:** uses named volume `market_data` and `.env.prod`.

### 7.1 Common commands

```bash
# Development
make up-dev           # alias for docker compose -f docker-compose.yml -f docker-compose.dev.yml up -d
make down             # stops any Compose stack

# Local production
docker compose -f docker-compose.yml -f docker-compose.prod.yml up -d
```

### 7.2 Additional flags

Edit `docker-compose.yml` to adjust live symbols/intervals. Remember that live ingestion currently supports only the `1m` interval.

## 8. AWS EC2 Deployment

### 8.1 Requirements

- Amazon Linux 2023 instance (t3.small recommended for testing).
- Security Group exposing SSH (22, restricted to your IP), HTTP (80), and optional 443.
- Public or Elastic IP.

### 8.2 Steps

1. **Key Pair + SG:** create a PEM key pair and SG with 22/80 rules.
2. **SSH:** `ssh -i <key>.pem ec2-user@<IP>`.
3. **Install Docker:**

   ```bash
   sudo dnf install -y docker
   sudo systemctl enable --now docker
   sudo usermod -aG docker ec2-user
   ```

4. **Data directory:**

   ```bash
   sudo mkdir -p /var/ttv/data
   sudo chown -R 1000:1000 /var/ttv/data
   ```

5. **Upload the image:**

   ```bash
   scp -i <key>.pem ttv-back.tar.gz ec2-user@<IP>:~
   ssh -i <key>.pem ec2-user@<IP> "docker load -i ~/ttv-back.tar.gz"
   ```

6. **Run the container:**

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

7. **Verify:** `curl http://<IP>/healthz`.

### 8.3 Cloudflare DNS

- Create an **A** record `api.tradingchart.ink` pointing to the public IP.
- Enable proxy (orange cloud) for protection and flexible TLS.

### 8.4 Common errors

| Error | Likely cause | Action |
| --- | --- | --- |
| `522` Cloudflare | Port 80 closed, container down, incorrect SG | Check SG, `docker ps`, `docker logs`, healthcheck. |
| `451` Binance/WS blocked | Region without access | Launch the instance in an allowed region (`sa-east-1` tested). |
| `Permission denied` DuckDB | Wrong volume ownership | `chown 1000:1000 /var/ttv/data`. |

### 8.5 Post-deploy checklist

- [ ] `curl http://<IP>/healthz` returns `200`.
- [ ] DNS `api.tradingchart.ink` resolves to the correct IP.
- [ ] Cloudflare returns HTTP 200 (no 522/520).
- [ ] `/api/v1/symbols` returns the expected symbols.
- [ ] `/api/v1/candles` yields data (>0 rows) after 2–3 minutes.
- [ ] `/stats` reports `ws_state=1` (connected).
- [ ] `last_msg_age_ms` < 2000.
- [ ] Logs show no Binance or DuckDB errors.
- [ ] CORS validates from the frontend (`Origin` allowed).
- [ ] Volume `/var/ttv/data` grows (new candles persisted).

## 9. Observability and Metrics

- **Logs:** use `docker logs -f tradingchart-api`. Levels `debug/info/warn/error`. Include ingestion details, DuckDB errors, and WS handshake issues.
- **Endpoint `/stats`:** exposes `uptime_seconds`, `ws_state` (1 = connected, 0 = disconnected), `last_msg_age_ms` (ms since last message), `reconnect_attempts_total`, `rest_catchup_candles_total`, and per-route metrics (`requests`, `p95_ms`, `p99_ms`).
- **Internal gauges/counters:** managed in `metrics::Registry`. Values reset when the process restarts.
- **Future ideas:** Prometheus/OpenMetrics exporter (see Roadmap).

## 10. Performance and Concurrency

- **Threads:** `HttpServer` starts `threads` workers (default 1) plus a dedicated keep-alive WS thread.
- **WS queue:** configurable limits (`max_msgs`, `max_bytes`, `stall_timeout`). Sessions exceeding limits close to protect the server.
- **Ingestion:** `LiveIngestor` spawns threads per symbol, handles retries, and paginated resyncs. `upsert_batch` groups up to 5000 rows per transaction.
- **Recommendations:**
  - CPU: minimum 2 vCPUs (t3.small) for stable ingestion.
  - RAM: ≥ 2 GiB (DuckDB in-memory structures and buffers).
  - Disk: SSD with reasonable IOPS; DuckDB grows with history.
  - Tune `--threads` based on CPU and REST load.

## 11. Security

- Keep CORS restricted in production (`https://www.tradingchart.ink`).
- Avoid logging credentials (not required for public Binance, but future additions should stay clean).
- Security Group: SSH 22 limited to your IP, HTTP 80 open, 443 if you enable TLS.
- For HTTPS:
  - Quick option: Cloudflare (Flexible/Full) + optional origin certificate.
  - Alternative: NGINX/Traefik reverse proxy or AWS ALB.
- Update the Docker image periodically with security patches (`apt-get update`).

## 12. Testing and QA


### 12.1 Smoke tests

```bash
# Health
curl -i http://localhost:8080/healthz


# Data

curl -i "http://localhost:8080/api/v1/candles?symbol=BTCUSDT&interval=1m&limit=2"

# Stats
curl -i http://localhost:8080/stats
```


### 12.2 Manual preflight


```bash
curl -i -X OPTIONS \
  -H "Origin: http://localhost:3000" \
  -H "Access-Control-Request-Method: GET" \
  http://localhost:8080/api/v1/symbols
```


### 12.3 Additional validations

- Run `scripts/probe_http.sh` against the host.
- Confirm `/stats` increases `reconnect_attempts_total` after a WS restart.
- Check that `routes` exposes consistent P95/P99 latencies.

## 13. Troubleshooting (FAQ)

| Problem | Cause | Solution |
| --- | --- | --- |
| `Permission denied` writing DuckDB | Volume owned by root | `chown -R 1000:1000 /data` or use `user: 1000:1000` in Compose. |
| CORS fails from local frontend | CORS disabled or wrong origin | Run with `--http.cors.enable=1 --http.cors.origin=http://localhost:3000`. |
| `/api/v1/candles` empty | Ingestion disconnected, wrong symbols, Binance region blocked | Check logs, confirm `--live-symbols`, validate egress connectivity. |
| Cloudflare `522` | Backend not responding | Check DNS, SG, container status, and curl directly to the IP. |
| `OPTIONS` returns 404 | Missing preflight handler | TODO: implement global OPTIONS support (see Roadmap). |
| DuckDB grows too much | Missing maintenance/compaction | Plan backups and cleanup (see Roadmap). |

## 14. Technical Roadmap

- [ ] Implement `OPTIONS` handler for full CORS support (configurable methods/headers).
- [ ] Allow multiple origins (`HTTP_CORS_ORIGIN` as list or pattern) with validation.
- [ ] Export Prometheus/OpenMetrics metrics.
- [ ] Add integration tests for the Router and HTTP server.
- [ ] Automate DuckDB backups/compaction; define rotation strategy.
- [ ] Support configuring flags directly via environment variables (`LIVE`, `STORAGE`, etc.).
- [ ] Document `tests/` and publish CI pipeline.

## 15. License and Credits

- **License:** TODO: define and document the repository's public license.
- **Credits:** Trading Viewer team. Add a detailed roster when available.

