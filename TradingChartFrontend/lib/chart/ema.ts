const EMA_COLOR_PALETTE = [
  "#6366f1",
  "#10b981",
  "#f97316",
  "#ec4899",
  "#14b8a6",
  "#facc15",
  "#0ea5e9",
  "#8b5cf6",
]

export function getEmaColor(period: number): string {
  if (!Number.isFinite(period)) {
    return EMA_COLOR_PALETTE[0]
  }
  const index = Math.abs(Math.trunc(period)) % EMA_COLOR_PALETTE.length
  return EMA_COLOR_PALETTE[index]
}

export { EMA_COLOR_PALETTE }
