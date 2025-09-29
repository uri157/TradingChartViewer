"use client"

import { useEffect, useRef } from "react"

import { tupleToDTO } from "@/lib/api/adapters"
import type { CandleDTO, CandleTuple } from "@/lib/api/types"
import { startLive } from "@/lib/ws/live"

export type LiveHandlers = {
  onAppend?: (c: CandleDTO) => void
  onReplace?: (c: CandleDTO) => void
  getLastTime?: () => number | null // segundos (unix)
}

const DEBUG_ENABLED = process.env.NEXT_PUBLIC_CHART_DEBUG === "1"

const logDebug = (...args: unknown[]) => {
  if (DEBUG_ENABLED && typeof window !== "undefined") {
    // eslint-disable-next-line no-console
    console.debug(...args)
  }
}

export function useLiveData(symbol: string, interval: string, handlers: LiveHandlers) {
  const handlersRef = useRef(handlers)
  const lastTsRef = useRef<number | null>(null) // ms

  // Siempre tener la última referencia de handlers
  useEffect(() => {
    handlersRef.current = handlers
  }, [handlers])

  // Semilla del último timestamp a partir del snapshot del chart
  useEffect(() => {
    lastTsRef.current = null
    const lastTimeSec = handlersRef.current.getLastTime?.()
    if (typeof lastTimeSec === "number" && Number.isFinite(lastTimeSec)) {
      lastTsRef.current = Math.floor(lastTimeSec * 1000)
    }
  }, [handlers])

  // Reset al cambiar símbolo o intervalo
  useEffect(() => {
    lastTsRef.current = null
    const lastTimeSec = handlersRef.current.getLastTime?.()
    if (typeof lastTimeSec === "number" && Number.isFinite(lastTimeSec)) {
      lastTsRef.current = Math.floor(lastTimeSec * 1000)
    }
  }, [symbol, interval])

  // Suscripción WS real
  useEffect(() => {
    if (!symbol || !interval) return

    const stop = startLive(
      (msg) => {
      // Filtrar por par/intervalo
        if (msg.symbol !== symbol || msg.interval !== interval) {
          logDebug("[LIVE] filtered out", msg.symbol, msg.interval)
          return
        }

        const h = handlersRef.current
        if (!h) return

        let candle: CandleDTO
        try {
          candle = tupleToDTO(msg.data as CandleTuple)
        } catch (error) {
          logDebug("[LIVE] invalid payload", error)
          return
        }
        const incomingTs = candle.t // ms

      // Alinear con el último tiempo conocido del chart (si existía)
        const lastTimeSec = h.getLastTime?.()
        if (typeof lastTimeSec === "number" && Number.isFinite(lastTimeSec)) {
          const lastMs = Math.floor(lastTimeSec * 1000)
          lastTsRef.current =
            lastTsRef.current == null ? lastMs : Math.max(lastTsRef.current, lastMs)
        }

        const lastTs = lastTsRef.current

      // Si llega una vela “nueva”, append; si llega misma vela (en curso), replace.
        if (lastTs == null || incomingTs > lastTs) {
          logDebug("[LIVE] append", candle.t)
          h.onAppend?.(candle)
          lastTsRef.current = incomingTs
        } else if (incomingTs === lastTs) {
          logDebug("[LIVE] replace", candle.t)
          h.onReplace?.(candle)
          lastTsRef.current = incomingTs
        } else {
          logDebug("[LIVE] stale", { incomingTs, lastTs })
        }
      // Si incomingTs < lastTs ignoramos (mensaje viejo o reordenado).
      },
      { subscribe: { symbol, interval } },
    )

    return () => {
      stop?.()
    }
  }, [symbol, interval])
}
