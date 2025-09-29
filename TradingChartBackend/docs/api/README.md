# Guía de integración de la API

## Resumen
- **Base URL por defecto:** `http://localhost:8080` (servidor ligado a `0.0.0.0`).
- **Versión/commit analizado:** código en `9467ede` (ejecutar `git rev-parse --short HEAD`).
- **Alcance:** endpoints HTTP sin autenticación y WebSocket de sólo difusión.

## Descubrimiento
| Método | Path | Descripción | Implementación |
| --- | --- | --- | --- |
| GET | `/healthz` | Verifica frescura del feed y estado del WS. 200 si todo OK, 500 con detalles cuando hay problemas. | `src/api/Router.cpp:16-28`, `src/api/Controllers.cpp:205-262` |
| GET | `/version` | Devuelve nombre y versión fija del backend. | `src/api/Router.cpp:17-18`, `src/api/Controllers.cpp:264-298` |
| GET | `/api/v1/candles` | Consulta velas históricas. Requiere `symbol` e `interval`. | `src/api/Router.cpp:18-19`, `src/api/Controllers.cpp:300-382` |
| GET | `/stats` | Estadísticas y métricas en vivo. | `src/api/Router.cpp:19-20`, `src/api/Controllers.cpp:384-448` |
| (cualquier) | `*` | Responde 404 `{"error":"not_found"}` para rutas desconocidas. | `src/api/Router.cpp:21-33` |

## Parámetros comunes
- `symbol` (`string`, requerido): admite alias `pair` o `ticker`. Responde 400 si falta o vacío.
- `interval` (`string`, requerido): alias `resolution` o `tf`. Valores válidos: `1m`, `5m`, `1h`, `1d`. Otros retornan 400.
- `from`/`fromTs`/`start`/`startTime` (`integer`): timestamp no negativo. Si `0` u omitido, el backend decide desde el inicio disponible.
- `to`/`toTs`/`end`/`endTime` (`integer`): timestamp no negativo, debe ser `>= from` cuando ambos existen.
- `limit`/`max`/`count` (`integer`): por defecto 200, máximo 5000; valores no positivos disparan 400.
- **Unidades temporales:** las respuestas convierten `ts` a milisegundos si el repositorio entrega segundos (ver `normalize_timestamp_ms` en src/api/Controllers.cpp:29-46). Los parámetros de entrada se pasan tal cual al repositorio; confirmar unidad esperada en cada backend (repos Duck/Legacy). _TODO: confirmar unidad de `from`/`to` para repositorios alternativos._

## Modelos/Esquemas
### `GET /api/v1/candles`
Respuesta exitosa (`200`):
```json
{
  "symbol": "BTCUSDT",
  "interval": "1m",
  "data": [
    [1735689600000, 42000.0, 42100.0, 41950.0, 42050.0, 12.34]
  ]
}
```
Estructura deducida de `candlesToJson` (src/api/Controllers.cpp:214-243). Sustituye los valores por datos reales. _TODO: confirmar ejemplo con datos reales almacenados._

### `GET /healthz`
- `200 OK`: `{ "status": "ok" }`.
- `500 Service Unavailable`: `{ "status": "error", "details": [{"issue": "stale_last_message", ...}, {"issue": "ws_down", ...}] }` según condiciones internas.

### `GET /version`
```json
{"name":"ttp-backend","version":"0.1.0"}
```
Literal estático.

### `GET /stats`
```json
{
  "uptime_seconds": 123.45,
  "threads": 4,
  "backend_active": true,
  "reconnect_attempts_total": 0,
  "rest_catchup_candles_total": 0,
  "ws_state": 1,
  "last_msg_age_ms": 1200,
  "routes": {
    "GET /api/v1/candles": {
      "requests": 42,
      "p95_ms": 12.3,
      "p99_ms": 15.6
    }
  }
}
```
Los campos `p95_ms`/`p99_ms` aparecen sólo si existen muestras (ver generación en src/api/Controllers.cpp:384-448). _TODO: confirmar métricas adicionales disponibles en `common::metrics::Registry`._

## Paginación y filtros
- `limit` controla la cantidad máxima de velas retornadas (paginación manual). No hay `offset`; combinar `from`/`to` para seguir ventanas.
- No existen filtros adicionales.

## Códigos de error y payload de error
- `400 Bad Request`: `{ "error": "<mensaje>" }` para validaciones de query (`symbol`, `interval`, timestamps, `limit`).
- `404 Not Found`: `{ "error": "not_found" }` para rutas sin handler.
- `500 Service Unavailable`: payload con `status` y `details` en `/healthz`. Otros controladores retornan 200 incluso con backend vacío.

## Rate limits
No hay límites de rate limiting implementados en HTTP ni WS.

## Autenticación
No se requiere autenticación; API pública local.

## CORS
No se envían cabeceras `Access-Control-*`; los navegadores tratarán las peticiones cross-origin como bloqueadas a menos que se proxyeen via Next.js o gateway propio.

## Ejemplos HTTP
### `curl`
```bash
curl -s "http://localhost:8080/healthz"
```
```bash
curl -s "http://localhost:8080/api/v1/candles?symbol=BTCUSDT&interval=1m&limit=5"
```
Agregar `from`/`to` en ms si se necesitan ventanas específicas.

### `fetch` (Next.js / Node)
```ts
export async function fetchCandles(symbol: string, interval: "1m" | "5m" | "1h" | "1d") {
  const params = new URLSearchParams({ symbol, interval, limit: "200" });
  const res = await fetch(`http://localhost:8080/api/v1/candles?${params.toString()}`);
  if (!res.ok) {
    const err = await res.json().catch(() => ({ error: res.statusText }));
    throw new Error(`API error ${res.status}: ${err.error ?? res.statusText}`);
  }
  return res.json() as Promise<{ symbol: string; interval: string; data: number[][] }>;
}
```
_Proveer un proxy en Next.js para evitar restricciones CORS._

## WebSocket `/ws`
### Flujo general
1. Handshake estándar (`Upgrade: websocket`). Rechaza peticiones sin headers obligatorios con 400.
2. Servidor envía `{"event":"welcome"}` inmediatamente tras registrar la sesión.
3. Todos los clientes reciben difusiones `type`:
   - `candle`: `{ "type": "candle", "symbol": "BTCUSDT", "interval": "1m", "data": [ts, o, h, l, c, v] }` para velas cerradas.
   - `resync_done`: `{ "type": "resync_done", "interval": "1m", "symbols": ["BTCUSDT", ...] }` tras catch-up REST exitoso.
4. El servidor ignora frames de texto entrantes (no hay comandos `subscribe`/`unsubscribe`) según sesión de lectura en src/api/WebSocketServer.cpp:594-666. _TODO: confirmar si se planea implementar filtros por cliente._

### Keepalive
- Control frames `ping` enviados cada `wsPingPeriodMs` (default 30 s). Si no llega `pong` en `wsPongTimeoutMs` (default 75 s) dos veces seguidas, la sesión se cierra (`pong_timeout`, código 1001).
- Los clientes deben responder automáticamente a `ping` (la mayoría de libs lo hacen). No hay pings de aplicación (`app_ping`).

### Límites y backpressure
- Frames entrantes mayores a 1 MiB provocan cierre inmediato (`read_error`).
- Inactividad >90 s cierra la conexión (`inactivity`, código 1001).
- Config se guarda para colas (`wsSendQueueMaxMsgs`, `wsSendQueueMaxBytes`, `wsStallTimeoutMs`), pero aún no hay lógica que la use en `WebSocketServer` (ver sección Deuda).
- Cierres adicionales: `write_error`/`read_error` usan código 1006; `server_shutdown` usa 1001.

### Reconexion sugerida
Aplicar backoff exponencial (p.ej. 1s, 2s, 5s, 10s) y resetear tras `welcome`. El servidor no mantiene estado por cliente, por lo que reconectar es seguro.

### Ejemplos de cliente WS
```ts
const WS_URL = "ws://localhost:8080/ws";

export function startWs(onCandle: (msg: any) => void) {
  let backoff = 1000;
  let ws: WebSocket | null = null;

  const connect = () => {
    ws = new WebSocket(WS_URL);
    ws.onopen = () => {
      backoff = 1000;
    };
    ws.onmessage = (event) => {
      try {
        const payload = JSON.parse(event.data as string);
        if (payload.type === "candle") {
          onCandle(payload);
        }
      } catch (err) {
        console.error("WS parse error", err);
      }
    };
    ws.onclose = () => {
      setTimeout(connect, backoff);
      backoff = Math.min(backoff * 2, 10000);
    };
    ws.onerror = () => {
      ws?.close();
    };
  };

  connect();
  return () => ws?.close();
}
```
_Node.js:_ usar `ws` con lógica equivalente (manejar pings automáticamente).

## Configuración de despliegue
Variables y flags principales (valores por defecto entre paréntesis):
- `PORT` / `--port` (8080) – puerto HTTP/WS.
- `LOG_LEVEL` / `--log-level` (`info`).
- `--threads` (1) – hilos del servidor HTTP.
- `--storage` (`legacy`) y `--duckdb` – backend de datos.
- WS keepalive/backpressure: `WS_PING_PERIOD_MS`/`--ws-ping-period-ms` (30000), `WS_PONG_TIMEOUT_MS`/`--ws-pong-timeout-ms` (75000), `WS_SEND_QUEUE_MAX_MSGS` (500), `WS_SEND_QUEUE_MAX_BYTES` (15728640), `WS_STALL_TIMEOUT_MS` (20000).
- Flags de ingesta (`--live`, `--live-symbols`, `--live-intervals`, backfill, etc.) afectan sólo la alimentación de datos para difusiones.

## Rutas internas y referencias
- Router HTTP: `src/api/Router.cpp`.
- Controladores: `src/api/Controllers.cpp` (healthz, version, candles, stats).
- Servidor HTTP base y handshake WS: `src/api/HttpServer.cpp`.
- Servidor WebSocket y ciclo de vida de sesiones: `src/api/WebSocketServer.cpp`.
- Difusiones WS (`candle`, `resync_done`): `src/app/LiveIngestor.cpp`.
- Configuración por flags/env: `src/common/Config.hpp`, `src/common/Config.cpp`.

### Deuda/Legacy
- La configuración de backpressure (`wsSendQueueMax*`, `wsStallTimeoutMs`) no se usa en `WebSocketServer`; existe una implementación separada `adapters/api/ws/SessionSendQueue`, pero no está integrada (ver src/api/WebSocketServer.cpp:418-447 y src/adapters/api/ws/SessionSendQueue.cpp:12-203). _TODO: confirmar plan para unificar colas WS._

## Checklist de pruebas rápidas
1. `curl http://localhost:8080/healthz` → esperar `{ "status": "ok" }` con backend activo.
2. `curl "http://localhost:8080/api/v1/candles?symbol=BTCUSDT&interval=1m&limit=2"` → verificar estructura `[ts,o,h,l,c,v]` en orden ascendente.
3. Iniciar un cliente WS (ver snippet) y confirmar recepción de `welcome` y posteriores mensajes `candle` al activar ingesta en vivo (`--live`).

