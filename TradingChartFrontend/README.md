# TradingView Lite Frontend

[![Next.js 15](https://img.shields.io/badge/Next.js-15.2.4-black?logo=next.js)](https://nextjs.org)
[![React 19](https://img.shields.io/badge/React-19.0.0-61dafb?logo=react)](https://react.dev)
[![TypeScript 5](https://img.shields.io/badge/TypeScript-5.0-blue?logo=typescript)](https://www.typescriptlang.org)
[![Tailwind CSS 4](https://img.shields.io/badge/Tailwind%20CSS-4.1-38bdf8?logo=tailwind-css)](https://tailwindcss.com)

## Table of Contents
1. [Project Summary](#1-project-summary)
2. [Architecture and Decisions](#2-architecture-and-decisions)
3. [Environment Variables](#3-environment-variables)
4. [Scripts and Tasks](#4-scripts-and-tasks)
5. [Local Development](#5-local-development)
6. [Build and Deployment](#6-build-and-deployment)
7. [API Integration](#7-api-integration)
8. [WebSockets (if applicable)](#8-websockets-if-applicable)
9. [UI/UX and Components](#9-uiux-and-components)
10. [Testing and Quality](#10-testing-and-quality)
11. [Performance](#11-performance)
12. [Observability and Monitoring](#12-observability-and-monitoring)
13. [Troubleshooting (FAQ)](#13-troubleshooting-faq)
14. [Roadmap and TODO](#14-roadmap-and-todo)
15. [License and Credits](#15-license-and-credits)
16. [Post-deploy Validation Checklist](#16-post-deploy-validation-checklist)

## 1. Project Summary
This frontend delivers a TradingView-inspired market experience with candlestick charts, MACD indicators, and controls for symbol and interval management. Routing relies on the Next.js 15 App Router and exposes dedicated views for the main dashboard, a health status screen, and user settings.【F:app/page.tsx†L1-L6】【F:app/health/page.tsx†L1-L7】【F:app/settings/page.tsx†L1-L7】

Core dependencies include Next.js 15, React 19, TypeScript 5, and Tailwind CSS 4, plus UI libraries (Radix, Geist, Lucide), state (Zustand), data fetching (TanStack Query), charting (lightweight-charts and custom helpers), and notifications (sonner).【F:package.json†L5-L70】 The base theme is implemented with Tailwind 4 tokens and dark variants tailored for charts and side panels.【F:app/globals.css†L1-L131】

Text-only flow diagram:
```
UI (Next.js + Zustand + React Query)
  → HTTP GET/POST against REST API (`/api/v1/*`)
  → WebSocket `/ws` for real-time ticks (when configured)
```

## 2. Architecture and Decisions
- **Folder structure.**
  - `app/`: App Router routes (`/`, `/health`, `/settings`) with a global layout that injects the QueryClient and analytics.【F:app/layout.tsx†L1-L34】【F:app/page.tsx†L1-L6】【F:app/health/page.tsx†L1-L7】【F:app/settings/page.tsx†L1-L7】
  - `components/`: Modular UI broken down into charting, headers, sidebar, feedback, and page-level composites.【F:components/pages/chart-page.tsx†L1-L200】【F:components/sidebar/symbol-list.tsx†L1-L75】
  - `hooks/`: Client-side hooks (mobile detection, live data, toast utilities).【F:hooks/use-live-data.ts†L1-L105】【F:hooks/use-mobile.ts†L1-L23】
  - `lib/`: Domain layer covering the API client and queries, global state, indicator math, utilities, and WebSocket helpers.【F:lib/api/client.ts†L1-L60】【F:lib/state/use-chart-store.ts†L1-L120】【F:lib/ws/live.ts†L1-L197】
  - `styles/` and `app/globals.css`: Tailwind 4 tokens and dark-theme overrides.【F:app/globals.css†L1-L131】

- **Shared state.** The `useChartStore` Zustand store persists the current symbol, interval, EMAs, and MACD preferences (toggle, height, and periods).【F:lib/state/use-chart-store.ts†L1-L120】 React Query (via `QueryProvider`) handles fetching with cache control and custom stale times.【F:lib/providers/query-provider.tsx†L1-L19】

- **Data fetching.** Hooks such as `useSnapshot`, `useSymbols`, and `useHealth` wrap API calls through `fetchJSON` hitting `/api/v1/*` endpoints. Responses are normalized with adapters that validate numeric values and timestamps.【F:lib/api/queries.ts†L1-L93】【F:lib/api/client.ts†L1-L60】【F:lib/api/adapters.ts†L1-L45】

- **Charts.**
  - `components/chart/candle-chart.tsx` plus `use-candle-chart` mount lightweight-charts, render candles and EMAs, and synchronize crosshair/zoom interactions.【F:components/chart/candle-chart.tsx†L1-L24】【F:components/chart/hooks/use-candle-chart.ts†L1-L200】
  - `indicator-chart.tsx` computes MACD using dedicated utilities (`lib/indicators/macd`).【F:components/chart/indicator-chart.tsx†L1-L120】【F:lib/indicators/macd.ts†L1-L196】
  - `chart-page.tsx` coordinates the initial snapshot, live merges, crosshair sync, and responsive layout (sidebar, mobile sheet, pair badge).【F:components/pages/chart-page.tsx†L84-L200】

- **Routing and metadata.** `RootLayout` sets the locale, default dark theme, and base metadata; App Router pages run on the client to avoid SSR limitations when WebSockets and DOM APIs are required.【F:app/layout.tsx†L1-L34】

## 3. Environment Variables
| Name | Required | Default | Purpose |
| --- | --- | --- | --- |
| `NEXT_PUBLIC_API_URL` | Yes (prod) | `http://localhost:8080` in development | HTTP base resolved by `resolveApiUrl` for REST calls.【F:lib/apiClient.ts†L1-L63】 |
| `NEXT_PUBLIC_API_BASE` | Optional | `/api` | Alternate prefix for internal proxies; used inside `lib/env`.【F:lib/env.ts†L1-L13】 |
| `NEXT_PUBLIC_API_BASE_URL` | Optional | TODO | Alias recommended when the deployment prefers an explicit public URL. Set it to the same value as `NEXT_PUBLIC_API_URL`. |
| `NEXT_PUBLIC_WS_URL` | Optional | Derived from `NEXT_PUBLIC_API_URL` + `/ws` | Direct WebSocket endpoint if it differs from the HTTP host.【F:lib/wsClient.ts†L1-L39】 |
| `NEXT_PUBLIC_WS_API_BASE` | Optional | `""` | Alternative override just for the WebSocket base URL.【F:lib/env.ts†L7-L9】 |
| `NEXT_PUBLIC_HTTP_API_BASE` / `HTTP_API_BASE` | Optional | `http://localhost:8080` | Explicit base for server-side requests or auxiliary tooling.【F:lib/env.ts†L3-L5】 |
| `NEXT_PUBLIC_DEFAULT_LIMIT` | Optional | `600` | Candle limit requested during the initial snapshot.【F:lib/env.ts†L11-L13】【F:lib/api/queries.ts†L40-L93】 |
| `NEXT_PUBLIC_CHART_DEBUG` | Optional | `"0"` | Enables verbose logging for chart and live data flows.【F:components/pages/chart-page.tsx†L30-L37】【F:hooks/use-live-data.ts†L15-L22】 |
| `NEXT_PUBLIC_INDICATOR_DEBUG` | Optional | `"0"` | Turns on internal traces for MACD calculations.【F:lib/indicators/macd.ts†L3-L8】 |

1. Create a `.env.local` file in the repository root.
2. Copy the example below and adjust URLs per environment (dev → local API, prod → `https://api.tradingchart.ink`).


```env
NEXT_PUBLIC_API_URL=https://api.tradingchart.ink
NEXT_PUBLIC_API_BASE_URL=https://api.tradingchart.ink
NEXT_PUBLIC_WS_URL=wss://api.tradingchart.ink/ws
NEXT_PUBLIC_DEFAULT_LIMIT=600

# Enable debug only when necessary

# NEXT_PUBLIC_CHART_DEBUG=1
# NEXT_PUBLIC_INDICATOR_DEBUG=1
```

> ⚠️ Never expose private secrets. Configure them as deployment secrets instead of committing them.

## 4. Scripts and Tasks
| Command | Purpose |
| --- | --- |
| `pnpm install` | Install dependencies using the pnpm v9 lockfile.【F:pnpm-lock.yaml†L1-L17】 |
| `pnpm dev` | Start the Next.js development server (hot reload, local WebSocket).【F:package.json†L5-L12】 |
| `pnpm build` | Compile the app for production (static-export friendly).【F:package.json†L5-L12】 |
| `pnpm export` | Trigger an explicit static export (`next export`) when only static HTML/JS is required.【F:package.json†L5-L8】 |
| `pnpm build:pages` | Alias for `next build` intended for Cloudflare Pages pipelines.【F:package.json†L5-L9】 |
| `pnpm start` | Serve the prebuilt production bundle with Next.js.【F:package.json†L5-L12】 |
| `pnpm lint` | Run the Next.js ESLint rules (not enforced during build but useful manually).【F:package.json†L5-L12】 |
| `pnpm test` | Execute type checks and Node-based adapter tests.【F:package.json†L5-L12】【F:lib/api/__tests__/adapters.test.ts†L1-L19】 |

**Requirements:** Node.js ≥18.18 (20 LTS recommended) and pnpm ≥9 to stay compatible with `lockfileVersion: '9.0'`. TODO: confirm the exact versions used in CI.

## 5. Local Development
1. Clone the repository and move into the project directory.
2. Install pnpm if missing (`npm install -g pnpm`).
3. Run `pnpm install`.
4. Create `.env.local` following the environment-variable section.
5. Start the dev server with `pnpm dev`.

By default the client uses `http://localhost:8080` as the API base (`NEXT_PUBLIC_API_URL`). Switch to the remote API by setting `NEXT_PUBLIC_API_URL=https://api.tradingchart.ink`.

**Quick verification:**
- Frontend: `curl -I http://localhost:3000/health` should return `200` and static HTML. If the static health route is stale, inspect `pnpm dev` logs.
- Remote/local API: `curl https://api.tradingchart.ink/api/healthz` or `curl http://localhost:8080/api/healthz` should respond with `{ "status": "ok" }`.

## 6. Build and Deployment
### 6.1 Cloudflare Pages (static export)
- Relevant Next.js config: `output: 'export'` and `images.unoptimized` are already enabled.【F:next.config.mjs†L1-L13】
- Recommended command: `pnpm run build` (or `pnpm run build:pages` to keep CI naming). The export produces `out/`.
- Pages output directory: `out`.
- Cloudflare Pages environment variables:
  - Production: `NEXT_PUBLIC_API_URL=https://api.tradingchart.ink`, `NEXT_PUBLIC_API_BASE_URL=https://api.tradingchart.ink`, `NEXT_PUBLIC_WS_URL=wss://api.tradingchart.ink/ws` (if applicable).
  - Previews: aim at staging endpoints (`https://staging.api...`).
- 25 MiB per-asset limit: lean on Next.js per-route code splitting and `dynamic()` imports for heavy tooling (for example, lazy-load advanced chart modules).

### 6.2 Alternatives
- **Vercel:** fully supported (`pnpm build` + `pnpm start`). Configure `NEXT_PUBLIC_API_URL` in Project Settings.
- **Generic static hosting:** run `pnpm export`, serve `out/` through a CDN, and configure rewrites so that `index.html` handles routing (Next export produces route-specific HTML).

## 7. API Integration
- Base URL comes from `NEXT_PUBLIC_API_URL`/`resolveApiUrl`. Dev: `http://localhost:8080`. Prod: `https://api.tradingchart.ink`.【F:lib/apiClient.ts†L1-L63】
- Endpoints in use:
  - `GET /api/v1/symbols` → symbol list.【F:lib/api/client.ts†L20-L22】
  - `GET /api/v1/intervals?symbol=BTCUSDT` → available intervals.【F:lib/api/client.ts†L24-L46】
  - `GET /api/v1/candles?symbol=BTCUSDT&interval=1m&limit=600` → candlestick snapshot.【F:lib/api/client.ts†L49-L60】
  - `GET /api/healthz` → health check consumed by the status view.【F:lib/api/queries.ts†L88-L93】【F:components/pages/health-page.tsx†L13-L161】

**Request/response samples:**

```bash
curl "$NEXT_PUBLIC_API_URL/api/v1/symbols"
```
```json
{
  "symbols": [
    { "symbol": "BTCUSDT", "base": "BTC", "quote": "USDT", "status": "active" }
  ]
}
```
```bash
curl "$NEXT_PUBLIC_API_URL/api/v1/candles?symbol=BTCUSDT&interval=1m&limit=3"
```
```json
{
  "symbol": "BTCUSDT",
  "interval": "1m",
  "data": [
    [1733742000, 43250.1, 43310.0, 43190.0, 43220.5, 12.3],
    [1733742060, 43220.5, 43280.0, 43200.0, 43275.0, 9.8],
    [1733742120, 43275.0, 43350.0, 43260.0, 43310.0, 15.2]
  ]
}
```


**CORS:** The API must expose `Access-Control-Allow-Origin` for frontend domains. When errors appear locally, stick to `http://localhost:3000` ↔ `http://localhost:8080` and enable CORS on the backend. For LAN scenarios, use proxies or secure tunnels.

**Debugging tips:**
- Timeouts: enable `NEXT_PUBLIC_CHART_DEBUG=1` and inspect the console to confirm WebSocket reconnection.【F:components/pages/chart-page.tsx†L30-L37】【F:lib/ws/live.ts†L8-L147】
- 5xx responses: look into `ApiError.body` logged in the console (captured by `fetchJSON`).【F:lib/api/fetch-json.ts†L1-L43】

## 8. WebSockets (if applicable)
The `useLiveData` hook opens a WebSocket via `startLive`, filters messages by symbol/interval, and decides whether to append or replace the last candle based on timestamps, with exponential backoff up to 10 s.【F:hooks/use-live-data.ts†L24-L105】【F:lib/ws/live.ts†L1-L197】 Relevant events:
- `event: "subscribe"` sent when the connection opens for the current symbol/interval.【F:lib/ws/live.ts†L163-L197】
- Messages shaped as `{ type: "candle", symbol, interval, data: [ts, o, h, l, c, v] }` are processed and converted to DTOs.【F:hooks/use-live-data.ts†L55-L93】

No other WebSocket channels are configured; if `NEXT_PUBLIC_WS_URL` is absent, it is derived from the HTTP host using `ws://` or `wss://` depending on the protocol.【F:lib/wsClient.ts†L16-L39】

## 9. UI/UX and Components
- **Theme and palette.** Dark-first design with tokens such as `--background`, `--chart-*`, and `--sidebar-*`; fonts rely on Geist Sans/Mono.【F:app/globals.css†L1-L131】【F:app/layout.tsx†L1-L34】
- **Libraries:** Radix (accessible primitives), Lucide (icons), sonner/toaster for notifications, Geist for typography.【F:package.json†L14-L69】【F:app/layout.tsx†L1-L34】
- **Key components:**

| Component | Main props | Description |
| --- | --- | --- |
| `<ChartPage />` | Uses store/queries internally | Orchestrates the snapshot, live data, crosshair sync, and responsive layout.【F:components/pages/chart-page.tsx†L84-L200】 |
| `<CandleChart data interval onCrosshairMove onReady />` | `data: CandleDTO[]`, `interval`, callbacks | Renders the main chart and exposes controls (zoom, append/replace).【F:components/chart/candle-chart.tsx†L1-L24】【F:components/chart/hooks/use-candle-chart.ts†L137-L200】 |
| `<IndicatorChart candles params onReady />` | `params: {fast, slow, signal}` | Draws MACD (lines + histogram) and synchronizes the crosshair.【F:components/chart/indicator-chart.tsx†L1-L120】 |
| `<TopBar />` | `children?` | Control bar with symbol selector, intervals, EMAs, and MACD toggle.【F:components/header/top-bar.tsx†L1-L120】 |
| `<SymbolList />` | `className?` | Filterable symbol list powered by React Query and Zustand.【F:components/sidebar/symbol-list.tsx†L18-L75】 |
| `<HealthPage />` | — | Static panel with uptime/API metrics.【F:components/pages/health-page.tsx†L13-L161】 |
| `<SettingsPage />` | — | Settings mock (theme, preferences).【F:components/pages/settings-page.tsx†L12-L175】 |

## 10. Testing and Quality
- **Automated tests:** only unit tests for `tupleToDTO` (candle normalization) exist.【F:lib/api/__tests__/adapters.test.ts†L1-L19】 TODO: add UI tests with Vitest/Testing Library.
- **Linting:** `pnpm lint` runs the Next.js ESLint rules (not blocking during build by configuration).【F:package.json†L5-L12】【F:next.config.mjs†L4-L9】 Resolve accessibility and convention issues before merging.
- **Type checking:** Next runs type checks during `pnpm build`, but CI can call `pnpm test` (includes `tsc -p tsconfig.tests.json`).【F:package.json†L5-L12】

## 11. Performance
- `images.unoptimized: true` skips native dependencies during static export.【F:next.config.mjs†L10-L13】 Use `next/image` only if you opt into edge optimization.
- Charts rely on memoization and buffers (`mergedCandles`, `useMemo`, `useRef`) to avoid unnecessary re-renders when live updates arrive.【F:components/pages/chart-page.tsx†L94-L176】
- The WebSocket pipeline applies backoff and filters repeated messages; MACD recalculation happens only when a candle changes.【F:hooks/use-live-data.ts†L24-L105】
- To keep assets below 25 MiB on Cloudflare Pages, load heavy components with `dynamic(() => import(...), { ssr: false })` when introducing new tooling (TODO for future modules).
- For Web Vitals, integrate the existing `@vercel/analytics` in the layout and consult its dashboard.【F:app/layout.tsx†L1-L34】 TODO: add automated Core Web Vitals reporting.

## 12. Observability and Monitoring
- Logging: toggle `NEXT_PUBLIC_CHART_DEBUG` or `NEXT_PUBLIC_INDICATOR_DEBUG` to inspect payloads in the console.【F:components/pages/chart-page.tsx†L30-L37】【F:lib/indicators/macd.ts†L3-L8】
- HTTP errors are wrapped inside `ApiError` with `status` and `body`, enabling UI banners and retry buttons (HealthPage, SymbolList, ChartPage).【F:lib/api/fetch-json.ts†L1-L43】【F:components/pages/health-page.tsx†L48-L161】【F:components/sidebar/symbol-list.tsx†L20-L66】
- The UI mounts a global `Toaster` (`sonner`) for instant feedback; call `useToast` for critical user events.【F:app/layout.tsx†L7-L30】

## 13. Troubleshooting (FAQ)
- **“Candles do not render.”** Ensure `/api/v1/candles` responds and that `NEXT_PUBLIC_API_URL` is correct. Enable `NEXT_PUBLIC_CHART_DEBUG=1` to inspect snapshot/live merges.【F:components/pages/chart-page.tsx†L30-L176】【F:lib/api/client.ts†L49-L60】
- **“Cloudflare Error 522.”** Confirm the API responds at `https://api.tradingchart.ink/api/healthz` and that Cloudflare DNS points to the correct port.
- **“Assets exceed 25 MiB.”** Split heavy modules with dynamic imports and avoid bundling unused libraries in the initial load.
- **“CORS on a dev LAN.”** Serve the frontend on `http://localhost:3000` and the API on `http://localhost:8080`, or use a reverse proxy adding the necessary headers.

## 14. Roadmap and TODO
- Add UI integration tests with Playwright/Vitest. TODO.
- Implement real persistence for settings in `SettingsPage` (store/API). TODO.
- Support additional indicators (RSI, volume profile) with lazy loading to keep bundles small. TODO.
- Internationalization (i18n) and language selector. TODO.
- Web Vitals metrics and automated reporting. TODO.

## 15. License and Credits
- TODO: define the license (no `LICENSE` file is present in the repository).
- Credits: initial interface generated with v0.app (see `generator: "v0.app"` metadata in the layout).【F:app/layout.tsx†L11-L15】

## 16. Post-deploy Validation Checklist
- [ ] `/health` loads without visual errors and shows “System Status” in green.【F:components/pages/health-page.tsx†L31-L161】
- [ ] Symbol selector lists entries and lets you change pairs (React Query + Zustand).【F:components/sidebar/symbol-list.tsx†L18-L66】
- [ ] Main chart renders candles and indicators for `BTCUSDT` with interval `1m` (initial snapshot).【F:components/pages/chart-page.tsx†L104-L200】
- [ ] Live updates arrive through WebSocket (watch the console with `NEXT_PUBLIC_CHART_DEBUG=1`).【F:hooks/use-live-data.ts†L24-L105】
- [ ] Settings page renders and respects the dark theme.【F:components/pages/settings-page.tsx†L12-L175】
- [ ] No CORS errors appear in the console when targeting the production API (`https://api.tradingchart.ink`).
- [ ] Final bundle on Pages stays below 25 MiB per asset (verify build/export logs).


