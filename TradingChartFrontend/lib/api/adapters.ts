import type { CandleDTO, CandleTuple, CandlesHttpResponse } from "./types"

function assertFiniteNumber(value: unknown, label: string): number {
  if (typeof value !== "number" || !Number.isFinite(value)) {
    throw new TypeError(`${label} must be a finite number`)
  }
  return value
}

function normalizeTimestamp(value: number): number {
  const msThreshold = 1_000_000_000_000 // 1e12 ~ Sep 2001 in ms
  const raw = Number.isInteger(value) ? value : Math.floor(value)
  if (raw < msThreshold) {
    return Math.floor(value * 1000)
  }
  return raw
}

export function tupleToDTO(tuple: CandleTuple): CandleDTO {
  if (!Array.isArray(tuple) || tuple.length !== 6) {
    throw new TypeError("Candle tuple must contain exactly 6 numeric values")
  }

  const [rawT, rawO, rawH, rawL, rawC, rawV] = tuple

  const t = normalizeTimestamp(assertFiniteNumber(rawT, "timestamp"))
  const o = assertFiniteNumber(rawO, "open")
  const h = assertFiniteNumber(rawH, "high")
  const l = assertFiniteNumber(rawL, "low")
  const c = assertFiniteNumber(rawC, "close")
  const v = assertFiniteNumber(rawV, "volume")

  return {
    t,
    o,
    h,
    l,
    c,
    v,
  }
}

export function candlesHttpToDTO(resp: CandlesHttpResponse): CandleDTO[] {
  return resp.data.map(tupleToDTO)
}
