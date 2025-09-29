"use client"

import { useQuery } from "@tanstack/react-query"

import { candlesHttpToDTO } from "./adapters"
import { apiGetCandles, apiGetIntervals, apiGetSymbols } from "./client"
import { fetchJSON } from "./fetch-json"
import type {
  SnapshotDTO,
  HealthDTO,
  Symbol,
  Interval,
  SymbolsResponse,
  IntervalsPlainResponse,
  IntervalsWithRangesResponse,
} from "./types"

const DEFAULT_LIMIT = Number(process.env.NEXT_PUBLIC_DEFAULT_LIMIT ?? 600)

function isAscendingCandles(candles: SnapshotDTO["candles"]): boolean {
  for (let i = 0; i < candles.length - 1; i++) {
    if (!(candles[i].t < candles[i + 1].t)) {
      return false
    }
  }
  return true
}

type SymbolMeta = SymbolsResponse["symbols"][number]

export function useSymbols() {
  return useQuery<SymbolsResponse, unknown, SymbolMeta[]>({
    queryKey: ["symbols"],
    queryFn: () => apiGetSymbols(),
    select: (data) => data.symbols,
    staleTime: 5 * 60 * 1000, // 5 minutes
  })
}

type IntervalMeta = IntervalsPlainResponse | IntervalsWithRangesResponse

export function useSymbolIntervals(symbol: string, includeRanges = false) {
  return useQuery<IntervalMeta>({
    queryKey: ["symbol-intervals", symbol, includeRanges],
    queryFn: () =>
      apiGetIntervals({
        symbol,
        includeRanges,
      }),
    enabled: Boolean(symbol),
    staleTime: 5 * 60 * 1000, // 5 minutes
  })
}

type UseSnapshotOptions = {
  enabled?: boolean
  from?: number
  to?: number
}

export function useSnapshot(
  symbol: string,
  interval: string,
  limit = DEFAULT_LIMIT,
  opts: UseSnapshotOptions = {},
) {
  const isClient = typeof window !== "undefined"
  const { enabled = true, from, to } = opts

  return useQuery({
    queryKey: ["snapshot", symbol, interval, limit, from ?? null, to ?? null],
    queryFn: async () => {
      const response = await apiGetCandles({
        symbol,
        interval: interval as Interval,
        limit,
        from,
        to,
      })
      const candles = candlesHttpToDTO(response)

      const snapshot: SnapshotDTO = {
        symbol: response.symbol,
        interval: response.interval as Interval,
        candles,
        meta: {
          source: "live",
          count: candles.length,
          generatedAt: Date.now(),
        },
      }

      // if (isDebug()) {
      //   const len = snapshot.candles.length
      //   const t0 = len ? snapshot.candles[0].t : "n/a"
      //   const tN = len ? snapshot.candles[len - 1].t : "n/a"
      //   const asc = len > 1 ? isAscendingCandles(snapshot.candles) : true
      //   console.log(
      //     `[query:snapshot] len=${len} t0(ms)=${t0} tN(ms)=${tN} asc=${asc} from=${
      //       from ?? "n/a"
      //     } to=${to ?? "n/a"} header=X-API-Version:1`,
      //     // Debug: verify snapshot payload before React Query caching.
      //   )
      // }

      return snapshot
    },
    enabled: !!symbol && !!interval && isClient && enabled,
    staleTime: 30 * 1000, // 30 seconds
  })
}

export function useCandles(
  symbol: Symbol,
  interval: Interval,
  from?: number,
  to?: number,
  limit = 600,
) {
  return useQuery({
    queryKey: ["candles", symbol, interval, from, to, limit],
    queryFn: async () => {
      const response = await apiGetCandles({
        symbol,
        interval,
        limit,
        from,
        to,
      })

      return candlesHttpToDTO(response)
    },
    enabled: !!symbol && !!interval,
    staleTime: 30 * 1000, // 30 seconds
  })
}

export function useHealth() {
  return useQuery({
    queryKey: ["health"],
    queryFn: () => fetchJSON<HealthDTO>("/api/healthz"),
    refetchInterval: 30 * 1000, // 30 seconds
  })
}
