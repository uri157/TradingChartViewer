"use client"

import type { CandleDTO } from "@/lib/api/types"
import { cn, type Interval } from "@/lib/utils"

import { useCandleChart, type CandleChartControls } from "./hooks/use-candle-chart"

export interface CandleChartProps {
  data: CandleDTO[]
  interval: Interval
  className?: string
  onCrosshairMove?: (payload: { price: number | null; time: number | null }) => void
  onReady?: (controls: CandleChartControls | null) => void
}

export type { CandleChartControls } from "./hooks/use-candle-chart"

export function CandleChart({ data, interval, className, onCrosshairMove, onReady }: CandleChartProps) {
  const { containerRef, isLoading } = useCandleChart({ data, interval, onCrosshairMove, onReady })

  if (isLoading && !data.length) {
    return (
      <div className={cn("flex items-center justify-center bg-card", className)}>
        <div className="text-muted-foreground">Loading chart...</div>
      </div>
    )
  }

  return <div ref={containerRef} className={cn("bg-card", className)} />
}
