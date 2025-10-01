"use client"

import {
  forwardRef,
  useCallback,
  useEffect,
  useImperativeHandle,
  useRef,
  type ComponentPropsWithoutRef,
  type MutableRefObject,
} from "react"
import type { IChartApi } from "lightweight-charts"

import {
  IndicatorChart,
  type IndicatorChartControls,
} from "@/components/chart/indicator-chart"

export interface MacdChartHandle {
  applySize: (height: number) => void
  measureRightAxisWidth: () => number
  setRightAxisWidth: (width: number) => void
  syncRightAxisWidth: (width: number) => void
  getChart: () => IChartApi | null
}

type BaseIndicatorProps = ComponentPropsWithoutRef<typeof IndicatorChart>

export interface MacdChartProps extends BaseIndicatorProps {
  chartRef?: MutableRefObject<IChartApi | null>
  className?: string
}

const MIN_DIMENSION = 2

const clampSize = (value: number): number => {
  if (!Number.isFinite(value)) return 0
  return Math.max(0, Math.floor(value))
}

export const MacdChart = forwardRef<MacdChartHandle, MacdChartProps>(function MacdChart(
  { chartRef, className, onReady, ...props },
  ref,
) {
  const wrapperRef = useRef<HTMLDivElement | null>(null)
  const controlsRef = useRef<IndicatorChartControls | null>(null)
  const heightRef = useRef(0)
  const widthRef = useRef(0)

  useEffect(() => {
    const wrapper = wrapperRef.current
    if (!wrapper) return

    const observer = new ResizeObserver((entries) => {
      const entry = entries[0]
      if (!entry) return
      const width = clampSize(entry.contentRect.width)
      const measuredHeight = clampSize(entry.contentRect.height)
      widthRef.current = width
      if (measuredHeight > 0) {
        heightRef.current = measuredHeight
        wrapper.style.height = `${measuredHeight}px`
      }
      const chart = controlsRef.current?.chart
      if (!chart) return
      const height = heightRef.current
      if (width < MIN_DIMENSION || height < MIN_DIMENSION) return
      chart.resize(width, height)
    })

    observer.observe(wrapper)

    return () => observer.disconnect()
  }, [])

  useImperativeHandle(
    ref,
    () => ({
      applySize: (height) => {
        const wrapper = wrapperRef.current
        const chart = controlsRef.current?.chart ?? null
        const nextHeight = clampSize(height)
        heightRef.current = nextHeight
        if (wrapper) {
          wrapper.style.height = nextHeight > 0 ? `${nextHeight}px` : "0px"
        }
        const width = widthRef.current || clampSize(wrapper?.clientWidth ?? 0)
        if (chart && width >= MIN_DIMENSION && nextHeight >= MIN_DIMENSION) {
          chart.resize(width, nextHeight)
        }
      },
      measureRightAxisWidth: () => {
        const chart = controlsRef.current?.chart
        if (!chart) return 0
        const priceScale = chart.priceScale("right")
        const actual = priceScale.width()
        const options = priceScale.options()
        return Math.max(actual, options.minimumWidth ?? 0)
      },
      setRightAxisWidth: (width) => {
        const chart = controlsRef.current?.chart
        if (!chart) return
        const nextWidth = Math.max(0, Math.round(width))
        chart.priceScale("right").applyOptions({
          minimumWidth: nextWidth,
          borderVisible: true,
          entireTextOnly: true,
        })
      },
      syncRightAxisWidth: (width) => {
        const chart = controlsRef.current?.chart
        if (!chart) return
        const nextWidth = Math.max(0, Math.round(width))
        chart.priceScale("right").applyOptions({
          minimumWidth: nextWidth,
          borderVisible: true,
          entireTextOnly: true,
        })
      },
      getChart: () => controlsRef.current?.chart ?? null,
    }),
    [],
  )

  const handleReady = useCallback(
    (controls: IndicatorChartControls | null) => {
      controlsRef.current = controls
      if (chartRef) {
        chartRef.current = controls?.chart ?? null
      }
      onReady?.(controls)
    },
    [chartRef, onReady],
  )

  return (
    <div
      ref={wrapperRef}
      className={className}
      style={{ position: "relative", width: "100%", height: "100%" }}
    >
      <IndicatorChart {...props} className="absolute inset-0" onReady={handleReady} />
    </div>
  )
})
