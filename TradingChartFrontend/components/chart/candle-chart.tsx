"use client"

import type { IChartApi } from "lightweight-charts"

import type { CandleDTO } from "@/lib/api/types"
import { getEmaColor } from "@/lib/chart/ema"
import { useEMAValues } from "@/lib/state/use-ema-values"
import { useChartStore } from "@/lib/state/use-chart-store"
import { cn, type Interval } from "@/lib/utils"

import { useCandleChart, type CandleChartControls } from "./hooks/use-candle-chart"

export interface CandleChartProps {
  data: CandleDTO[]
  interval: Interval
  className?: string
  onCrosshairMove?: (payload: { price: number | null; time: number | null }) => void
  onReady?: (controls: CandleChartControls | null) => void
  onRequestAutoStick?: (chart: IChartApi) => void
}

export type { CandleChartControls } from "./hooks/use-candle-chart"

export function CandleChart({
  data,
  interval,
  className,
  onCrosshairMove,
  onReady,
  onRequestAutoStick,
}: CandleChartProps) {
  const { containerRef, isLoading } = useCandleChart({
    data,
    interval,
    onCrosshairMove,
    onReady,
    onRequestAutoStick,
  })
  const emaPeriods = useChartStore((state) => state.emaPeriods)
  const emaValues = useEMAValues((state) => state.emaValues)

  if (isLoading && !data.length) {
    return (
      <div className={cn("flex items-center justify-center bg-card", className)}>
        <div className="text-muted-foreground">Loading chart...</div>
      </div>
    )
  }

  return (
    <div className={cn("relative bg-card", className)}>
      <div
        className="absolute left-2 top-2 z-10 flex flex-wrap gap-2 text-xs font-medium"
        style={{ pointerEvents: "none" }}
      >
        {emaPeriods.map((period) => {
          const rawValue = emaValues[period]
          const formattedValue = rawValue != null ? formatEmaValue(rawValue) : "—"
          const color = getEmaColor(period)
          return (
            <span
              key={period}
              className="rounded-md border px-2 py-1"
              style={{
                borderColor: color,
                color,
                backgroundColor: "rgba(0, 0, 0, 0.45)",
              }}
            >
              {`EMA ${period} — ${formattedValue}`}
            </span>
          )
        })}
      </div>
      <div ref={containerRef} className="h-full w-full" />
    </div>
  )
}

function formatEmaValue(value: number) {
  if (!Number.isFinite(value)) {
    return "—"
  }

  const absolute = Math.abs(value)

  if (absolute >= 1000) {
    return value.toLocaleString(undefined, { maximumFractionDigits: 2 })
  }

  if (absolute >= 1) {
    return value.toLocaleString(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 2 })
  }

  return value.toLocaleString(undefined, { minimumFractionDigits: 4, maximumFractionDigits: 8 })
}
