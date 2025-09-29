export type Interval = "1m" | "5m" | "1h" | "1d"

export type CandleTuple = [number, number, number, number, number, number]

export interface CandlesHttpResponse {
  symbol: string
  interval: string
  data: CandleTuple[]
}

export interface CandleDTO {
  t: number
  o: number
  h: number
  l: number
  c: number
  v: number
}

export interface SymbolsResponse {
  symbols: Array<{
    symbol: string
    base?: string
    quote?: string
    status?: "active" | "inactive"
  }>
}

export type IntervalsPlainResponse = {
  symbol: string
  intervals: string[]
}

export type IntervalsWithRangesResponse = {
  symbol: string
  intervals: Array<{
    name: string
    from?: number
    to?: number
  }>
}

export type Timestamp = number
export type Symbol = string

export interface SnapshotDTO {
  symbol: string
  interval: Interval
  candles: CandleDTO[]
  meta: {
    source: "live" | "binance"
    count: number
    generatedAt: number
  }
}

export interface LiveTickDTO {
  symbol: string
  interval: Interval
  candle: CandleDTO
  isClosed: boolean
}

export type SymbolsDTO = string[]

export interface HealthDTO {
  status: "ok"
  duckdb: "ready"
  uptimeSec: number
}
