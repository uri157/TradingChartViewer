Aquí tenés un README.md unificado y limpio (listo para pegar):

```markdown
# The Trading Project (TTP)

TTP is a **C++17 desktop terminal** that renders streaming and historical cryptocurrency market data on an **interactive candlestick chart**. It combines **SFML**-based rendering with **Boost.Asio/Beast** and **OpenSSL** for network connectivity, pulling market data from Binance-style REST and WebSocket endpoints through a configurable dependency-injection bootstrap layer.

---

## Key features

- **Live and historical market synchronisation.** A `SyncOrchestrator` seeds local storage with REST backfills, streams live kline updates with exponential backoff, and republishes snapshots through the event bus whenever new candles arrive.
- **Local persistence and in-memory caching.** `DatabaseEngine`, `PriceDataManager`, and `PriceDataTimeSeriesRepository` normalise, persist, and surface OHLCV data while a thread-safe `SeriesCache` provides fast snapshots for the renderer.
- **Interactive charting UI.** The `ChartController` drives SFML rendering, handling pan/zoom input, backfill requests, cursor overlays, and the `RenderSnapshot` data model that powers axes, wicks, crosshairs, and indicator overlays. The grid scene augments the chart with a Matrix-style background and UI chrome.
- **Indicator engine with warm-up reuse.** The indicator coordinator computes and caches EMA series, automatically fetching warm-up ranges and scheduling async recomputations to minimise redraw latency.
- **Event-driven architecture.** A custom event bus propagates SFML input and series updates, enabling decoupled UI components and background workers to coordinate refreshes and backfills.
- **Structured logging & diagnostics.** Category-aware logging with CLI/environment overrides, optional sanitizer builds, and a `--diag` mode surface runtime state for troubleshooting.

---

## Architecture at a glance

- **Bootstrap & configuration (`bootstrap/`, `config/`).** A lightweight DI container wires SFML, storage, networking, and UI services, creating data/cache directories on startup. CLI flags, environment variables, and optional key–value config files feed a shared `Config` instance (precedence: **CLI > ENV > file > defaults**).
- **Application layer (`app/`).** `Application` owns the render loop, event dispatch, and DI lookups; `SyncOrchestrator` manages backfills, live subscriptions, and snapshot publication; `ChartController` converts user input into viewport updates and backfill requests; `RenderSnapshotBuilder` transforms candle series plus indicator data into draw-ready primitives and UI state.
- **Domain & core utilities (`domain/`, `core/`).** Domain types describe candles, intervals, repositories, and live stream contracts, while core utilities provide the series cache, viewport math, render snapshot schema, timestamp helpers, and the event bus abstraction.
- **Infrastructure (`infra/`).**
  - `exchange/ExchangeGateway` implements `MarketSource` via Boost.Beast REST calls, WebSocket streaming with exponential backoff, idle detection, and jittered reconnects.
  - `net/WebSocketClient` is a lighter WS client used to stream raw price data into the database engine.
  - `storage/` bridges binary persistence (`PriceDataManager`) with domain repositories (`PriceDataTimeSeriesRepository`) and real-time cache/observer logic (`DatabaseEngine`).
  - `tools/CryptoDataFetcher` provides REST historical fetch helpers.
  - `storage/PriceData` defines the on-disk record layout.
- **Indicators (`indicators/`).** Pluggable indicator calculators (currently EMA) with caching, version tracking, and optional async recomputation orchestrated through repository snapshots.
- **User interface (`ui/`).** Rendering is command-queued via `RenderManager`; the chart scene composes grids, cursors, and backgrounds; `MatrixRain` renders animated glyph streams; `ResourceProvider` lazily loads fonts/textures with fallbacks; `Cursor` draws custom crosshairs tied to event bus updates.

---

## Repository layout

```

app/         UI orchestration, sync logic, render snapshot construction
bootstrap/   DI container, service registration, program entry point
config/      Configuration structures and loaders
core/        Event bus, caches, rendering primitives, viewport maths
domain/      Market abstractions and repository contracts
infra/       Networking, exchange gateway, storage, tooling backends
indicators/  Indicator engines and coordinators
ui/          Rendering commands, scenes, resources, visual components
data/        Default data directory (runtime-populated)

````

---

## Build & dependencies

### Ubuntu

```sh
sudo apt update
sudo apt install -y build-essential g++ make pkg-config \
  libsfml-dev libssl-dev \
  libboost-system-dev libboost-json-dev
````

### Build targets

```sh
make -j"$(nproc)"                # Debug build with warnings and -g (default)
make SANITIZE=address -j         # Enable ASAN instrumentation
make SANITIZE=address,undefined -j
make MODE=release -j             # Optimized build (-O3 -DNDEBUG) without sanitizers
make WERROR=1 -j                 # Optional: promote warnings to errors
make clean                       # Remove obj/bin
```

The binary is emitted at `bin/main`; object files are under `obj/`.

---

## Running the application

```sh
./bin/main --symbol ETHUSDT --interval 5m --log-level debug
```

**Available CLI options** include symbol/interval selection, data & cache directories, window geometry/fullscreen, REST/WS overrides, log level, and helper flags (`--help`, `--version`, `--diag`).

**Configuration precedence:**
CLI → ENV (`TTP_SYMBOL`, `TTP_INTERVAL`, `TTP_DATA_DIR`, `TTP_CACHE_DIR`, `TTP_WINDOW_W`, `TTP_WINDOW_H`, `TTP_FULLSCREEN`, `TTP_LOG_LEVEL`, `TTP_REST_HOST`, `TTP_WS_HOST`, `TTP_WS_PATH`) → key=value file passed via `--config`/`TTP_CONFIG` → built-in defaults.

---

## Runtime logging

* **Default level:** `info`
* **CLI flags:**

  * `--log-level=<trace|debug|info|warn|error>`
  * `--trace` (forces `TRACE`)
  * `--debug` (forces `DEBUG`)
* **Environment override:** `TTP_LOG_LEVEL=trace|debug|info|warn|error`
* **Priority:** CLI level > `--trace/--debug` > `TTP_LOG_LEVEL` > default

**Examples:**

```sh
./bin/main
TTP_LOG_LEVEL=debug ./bin/main
./bin/main --trace
ASAN_OPTIONS=abort_on_error=1:symbolize=1:detect_leaks=1 ./bin/main --trace
```

At startup the application prints the effective level and sanitizer configuration.

---

## Data flow & storage

* At startup the DI configurator ensures `dataDir` and `cacheDir` exist, then registers shared services for rendering, networking, storage, and UI primitives.
* Application stores time-series data under `<cacheDir>/<symbol>_<interval>_timeseries.bin`, backed by `PriceDataTimeSeriesRepository` and the on-disk `PriceData` layout.
* `DatabaseEngine` spawns a warm-up thread that repeatedly fetches historical batches, persists them via `PriceDataManager`, maintains a ring buffer cache, and notifies observers of new candles and price limits. Incoming WebSocket ticks are normalised and either replace the open candle or append a closed record, triggering observer callbacks and optional disk writes.
* `SyncOrchestrator` merges repository snapshots into the `SeriesCache`, publishes `SeriesUpdated` events, and throttles live updates to balance responsiveness with render cost.

---

## Rendering & interaction

* `ChartController` converts mouse drags, wheel zoom, and keyboard pans into viewport adjustments, requests snapshot rebuilds, and issues backfill requests when the user scrolls beyond loaded history.
* `RenderSnapshotBuilder` and `core::RenderSnapshot` describe every drawable element (candles, wicks, axes, labels, crosshair, indicator overlays, UI state), allowing the render manager to schedule layered draw commands.
* The grid scene composes Matrix Rain backgrounds, UI labels, and cursor sprites supplied by the resource provider, which falls back to system fonts/textures when bundled assets are missing.

---

## Indicators

`IndicatorCoordinator` augments candle slices with warm-up data from the repository, caches EMA computations keyed by parameters, and supports both synchronous updates and deferred background recomputes to keep UI refreshes snappy.

---

## Networking

`ExchangeGateway` provides a resilient `MarketSource` implementation—issuing REST paginated klines for backfill and maintaining a TLS WebSocket session that parses Binance-style kline payloads, buffers closed candles, and handles idle timeouts plus jittered exponential reconnects.
A lighter `infra::net::WebSocketClient` streams raw prices directly into the database engine, sharing similar reconnect logic.

---

## Logging & observability

The logging subsystem supports level parsing, category tagging (`NET/DATA/CACHE/SNAPSHOT/RENDER/UI/DB`), timestamped output, and global level changes. CLI options, environment variables, and `TTP_DEBUG` toggle tracing, while sanitizer flags baked into the build surface runtime instrumentation when needed.

---

## Contributing

Issues and PRs are welcome. This README summarises the major systems so new contributors can orient themselves quickly, build the project, and understand how real-time market data flows from the network to on-screen visualisations.

```
```
