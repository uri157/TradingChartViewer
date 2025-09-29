"use client"

import { resolveWsUrl } from "../wsClient"

type MessageHandler = (payload: unknown) => void
export type CleanupFn = () => void

const MAX_BACKOFF_MS = 10_000
const INITIAL_BACKOFF_MS = 1_000
const DEBUG_ENABLED = process.env.NEXT_PUBLIC_CHART_DEBUG === "1"

type ConnectOptions = {
  onOpen?: (socket: WebSocket) => void
}

const logDebug = (...args: unknown[]) => {
  if (DEBUG_ENABLED && typeof window !== "undefined") {
    // eslint-disable-next-line no-console
    console.debug(...args)
  }
}

function parseJSON(text: string): unknown | null {
  if (!text) return null
  try {
    return JSON.parse(text) as unknown
  } catch (error) {
    logDebug("[WS] invalid payload", error)
    return null
  }
}

export function connectLive(onMessage: MessageHandler, options: ConnectOptions = {}): CleanupFn {
  if (typeof window === "undefined") {
    return () => {}
  }

  let ws: WebSocket | null = null
  let shouldReconnect = true
  let backoff = INITIAL_BACKOFF_MS
  let reconnectTimer: ReturnType<typeof setTimeout> | null = null

  const clearReconnectTimer = () => {
    if (reconnectTimer != null) {
      clearTimeout(reconnectTimer)
      reconnectTimer = null
    }
  }

  const scheduleReconnect = () => {
    if (!shouldReconnect) {
      return
    }

    clearReconnectTimer()
    reconnectTimer = setTimeout(() => {
      reconnectTimer = null
      connect()
    }, backoff)
    backoff = Math.min(backoff * 2, MAX_BACKOFF_MS)
  }

  const handlePayload = (payload: unknown) => {
    if (payload == null) {
      return
    }
    logDebug("[WS] msg", payload)
    onMessage(payload)
  }

  const handleMessage = (event: MessageEvent<unknown>) => {
    const data = event.data

    if (typeof data === "string") {
      handlePayload(parseJSON(data))
      return
    }

    if (data instanceof Blob) {
      data
        .text()
        .then((text) => {
          handlePayload(parseJSON(text))
        })
        .catch((error) => {
          logDebug("[WS] blob read error", error)
        })
      return
    }

    if (data instanceof ArrayBuffer) {
      const decoder = new TextDecoder()
      const text = decoder.decode(data)
      handlePayload(parseJSON(text))
    }
  }

  const connect = () => {
    if (!shouldReconnect) {
      return
    }

    try {
      const url = resolveWsUrl()
      ws = new WebSocket(url)
    } catch (error) {
      logDebug("[WS] connection error", error)
      scheduleReconnect()
      return
    }

    ws.onopen = () => {
      logDebug("[WS] open")
      backoff = INITIAL_BACKOFF_MS
      options.onOpen?.(ws as WebSocket)
    }

    ws.onmessage = handleMessage

    ws.onerror = (event) => {
      logDebug("[WS] error", event)
      ws?.close()
    }

    ws.onclose = () => {
      logDebug("[WS] close")
      if (!shouldReconnect) {
        return
      }
      scheduleReconnect()
    }
  }

  const cleanup: CleanupFn = () => {
    shouldReconnect = false
    clearReconnectTimer()
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.close()
    } else {
      ws?.close()
    }
    ws = null
  }

  connect()

  return cleanup
}

export type WsCandleMsg = {
  type: "candle"
  symbol: string
  interval: string
  data: [number, number, number, number, number, number]
}

type AnyMsg = WsCandleMsg & Record<string, unknown>

type StartLiveOptions = {
  subscribe?: { symbol: string; interval: string }
}

export function startLive(onCandle: (msg: WsCandleMsg) => void, options: StartLiveOptions = {}): CleanupFn {
  return connectLive(
    (payload) => {
      if (!payload || typeof payload !== "object") {
        return
      }

      const message = payload as AnyMsg
      if (message.type === "candle") {
        onCandle(message as WsCandleMsg)
      }
    },
    {
      onOpen: (socket) => {
        const { subscribe } = options
        if (!subscribe) {
          return
        }

        try {
          socket.send(
            JSON.stringify({
              event: "subscribe",
              symbol: subscribe.symbol,
              interval: subscribe.interval,
            }),
          )
          logDebug("[WS] subscribe", subscribe)
        } catch (error) {
          logDebug("[WS] subscribe error", error)
        }
      },
    },
  )
}
