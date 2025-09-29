import { fetchJSON } from "./fetch-json"
import type {
  CandleTuple,
  CandlesHttpResponse,
  IntervalsPlainResponse,
  IntervalsWithRangesResponse,
  SymbolsResponse,
} from "./types"

export type { CandleTuple }

type ApiGetCandlesParams = {
  symbol: string
  interval: "1m" | "5m" | "1h" | "1d"
  limit?: number
  from?: number
  to?: number
}

export async function apiGetSymbols(): Promise<SymbolsResponse> {
  return fetchJSON<SymbolsResponse>("/api/v1/symbols")
}

type ApiGetIntervalsParams = {
  symbol: string
  includeRanges?: boolean
}

export async function apiGetIntervals({
  symbol,
  includeRanges,
}: ApiGetIntervalsParams): Promise<
  IntervalsPlainResponse | IntervalsWithRangesResponse
> {
  const qs = new URLSearchParams()
  qs.set("symbol", symbol)
  if (includeRanges) {
    qs.set("include_ranges", "true")
  }

  const query = qs.toString()
  const suffix = query ? `?${query}` : ""

  return fetchJSON<IntervalsPlainResponse | IntervalsWithRangesResponse>(
    `/api/v1/intervals${suffix}`,
  )
}

export async function apiGetCandles(
  params: ApiGetCandlesParams,
): Promise<CandlesHttpResponse> {
  const qs = new URLSearchParams()
  qs.set("symbol", params.symbol)
  qs.set("interval", params.interval)
  if (typeof params.limit === "number") qs.set("limit", String(params.limit))
  if (typeof params.from === "number") qs.set("from", String(params.from))
  if (typeof params.to === "number") qs.set("to", String(params.to))

  return fetchJSON<CandlesHttpResponse>(`/api/v1/candles?${qs.toString()}`)
}
