import type { Time, WhitespaceData } from "lightweight-charts"

export const GHOST_PAD_BARS = (() => {
  const raw = process.env.NEXT_PUBLIC_CHART_GHOST_PAD
  const parsed = raw != null ? Number.parseInt(raw, 10) : NaN
  if (Number.isFinite(parsed) && parsed >= 0) {
    return parsed
  }
  return 120
})()

export function buildGhostTimes(
  timesSec: number[],
  padBars: number,
  intervalSec: number,
): number[] {
  if (!timesSec.length || padBars <= 0 || intervalSec <= 0) return []
  const first = timesSec[0]
  const last = timesSec[timesSec.length - 1]

  const left: number[] = []
  for (let i = padBars; i >= 1; i--) left.push(first - i * intervalSec)

  const right: number[] = []
  for (let i = 1; i <= padBars; i++) right.push(last + i * intervalSec)

  return [...left, ...right]
}

export function toWhitespaceData(timesSec: number[]): WhitespaceData<Time>[] {
  return timesSec.map<WhitespaceData<Time>>((timeSec) => ({ time: timeSec as Time }))
}
