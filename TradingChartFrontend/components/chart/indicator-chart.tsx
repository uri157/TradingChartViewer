"use client"

import { useCallback, useEffect, useRef, useState } from "react"
import {
  ColorType,
  HistogramSeries,
  LineSeries,
  LineStyle,
  createChart,
  type HistogramData,
  type IChartApi,
  type ISeriesApi,
  type LineData,
  type MouseEventParams,
  type Time,
} from "lightweight-charts"

import type { CandleDTO } from "@/lib/api/types"
import { intervalToSeconds, resolveCssColor, type Interval } from "@/lib/utils"
import { computeMACD, type MACDPoint } from "@/lib/indicators/macd"
import { useIndicatorMACD, type IndicatorValues } from "@/components/chart/hooks/use-indicator-macd"
import { ilog, igroup, igroupEnd } from "@/lib/debug/ilog"
import { buildGhostTimes, GHOST_PAD_BARS, toWhitespaceData } from "@/lib/chart/ghost"
import { BASE_TIME_SCALE_OPTIONS, syncTimeScales } from "@/lib/chart/sync"

export interface IndicatorChartControls {
  chart: IChartApi
  setExternalCrosshair: (time: number | null) => void
  getValuesForTime: (time: number) => IndicatorValues | null
  syncWith: (chart: IChartApi | null) => void
  setDataMACD: (data: {
    macd: LineData<Time>[]
    signal: LineData<Time>[]
    hist: HistogramData<Time>[]
  }) => void
  updateMACD: (point: { time: number; macd?: number; signal?: number; hist?: number }) => void
}

interface IndicatorChartProps {
  candles: CandleDTO[]
  interval: Interval
  params: { fast: number; slow: number; signal: number }
  className?: string
  onReady?: (controls: IndicatorChartControls | null) => void
  onCrosshairMove?: (payload: { time: number | null }) => void
}

const POSITIVE_HISTOGRAM_COLOR = "rgba(34,197,94,3)"
const NEGATIVE_HISTOGRAM_COLOR = "rgba(239,68,68,3)"

const mapMACDToLineData = (points: MACDPoint[]): LineData<Time>[] =>
  points.map((point) => ({ time: point.time as Time, value: point.value }))

const mapMACDToHistogramData = (points: MACDPoint[]): HistogramData<Time>[] =>
  points.map((point) => ({
    time: point.time as Time,
    value: point.value,
    color: point.value >= 0 ? POSITIVE_HISTOGRAM_COLOR : NEGATIVE_HISTOGRAM_COLOR,
  }))

function applyMACDData(
  hs: ISeriesApi<"Histogram">,
  ms: ISeriesApi<"Line">,
  ss: ISeriesApi<"Line">,
  candles: CandleDTO[],
  params: { fast: number; slow: number; signal: number },
) {
  if (!candles?.length) {
    ms.setData([])
    ss.setData([])
    hs.setData([])
    ilog("indic:setData:empty")
    return
  }

  const times = candles.map((candle) => Math.floor(candle.t / 1000))
  const closes = candles.map((candle) => candle.c)
  const output = computeMACD(closes, times, params.fast, params.slow, params.signal)

  ilog("indic:setData:stats", {
    len: {
      macd: output.macd.length,
      signal: output.signal.length,
      hist: output.hist.length,
    },
    t0: times[0],
    tN: times.at(-1),
  })

  ms.setData(mapMACDToLineData(output.macd))
  ss.setData(mapMACDToLineData(output.signal))
  hs.setData(mapMACDToHistogramData(output.hist))
}

export function IndicatorChart({ candles, interval, params, className, onReady, onCrosshairMove }: IndicatorChartProps) {
  const containerRef = useRef<HTMLDivElement | null>(null)
  const chartRef = useRef<IChartApi | null>(null)
  const macdSeriesRef = useRef<ISeriesApi<"Line"> | null>(null)
  const signalSeriesRef = useRef<ISeriesApi<"Line"> | null>(null)
  const histSeriesRef = useRef<ISeriesApi<"Histogram"> | null>(null)
  const ghostSeriesRef = useRef<ISeriesApi<"Line"> | null>(null)
  const crosshairHandlerRef = useRef<((param: MouseEventParams<Time>) => void) | null>(null)
  const timeSyncCleanupRef = useRef<(() => void) | null>(null)
  const linkedChartRef = useRef<IChartApi | null>(null)
  const controlsRef = useRef<IndicatorChartControls | null>(null)
  const [isReady, setIsReady] = useState(false)
  const sizeOkRef = useRef(false)
  const resizeRafRef = useRef<number | null>(null)
  const lastSetDataAtRef = useRef(0)

  const macdControls = useIndicatorMACD({
    candles,
    params,
    chartRef,
    series: { macd: macdSeriesRef, signal: signalSeriesRef, hist: histSeriesRef },
    ready: isReady,
  })

  useEffect(() => {
    const container = containerRef.current
    if (!container) return

    // const backgroundColor = resolveCssColor("hsl(var(--background))")
    const backgroundColor = "transparent"
    // container.style.backgroundColor = backgroundColor

    const resizeObserver = new ResizeObserver((entries) => {
      const entry = entries[0]
      if (!entry) return
      const width = Math.floor(entry.contentRect.width)
      const height = Math.floor(entry.contentRect.height)

      ilog("indic:resize:measure", { w: width, h: height }, 33)

      if (resizeRafRef.current != null) {
        cancelAnimationFrame(resizeRafRef.current)
      }

      resizeRafRef.current = requestAnimationFrame(() => {
        resizeRafRef.current = null

        const valid = Number.isFinite(width) && Number.isFinite(height) && width >= 2 && height >= 2
        if (!valid) {
          sizeOkRef.current = false
          return
        }

        sizeOkRef.current = true

        let chart = chartRef.current
        if (!chart) {
          const axisTextColor = resolveCssColor("var(--chart-axis-text)")
          const gridLineColor = resolveCssColor("var(--chart-grid-line)")
          const accentColor = resolveCssColor("hsl(var(--accent))")

          chart = createChart(container, {
            width,
            height,
            layout: {
              background: { type: ColorType.Solid, color: backgroundColor },
              textColor: axisTextColor,
              fontSize: 12,
              fontFamily: "var(--font-geist-sans)",
              attributionLogo: false,
            },
            grid: {
              vertLines: { visible: false, color: gridLineColor, style: LineStyle.Solid },
              horzLines: { visible: true, color: gridLineColor, style: LineStyle.Solid },
            },
            crosshair: {
              mode: 1,
              vertLine: { color: accentColor, width: 1, style: LineStyle.Solid },
              horzLine: { color: accentColor, width: 1, style: LineStyle.Solid },
            },
            rightPriceScale: {
              borderColor: gridLineColor,
              textColor: axisTextColor,
              scaleMargins: { top: 0.1, bottom: 0.1 },
            },
            leftPriceScale: { visible: false },
            timeScale: {
              ...BASE_TIME_SCALE_OPTIONS,
              visible: false,
              borderVisible: false,
              borderColor: gridLineColor,
              timeVisible: false,
              secondsVisible: false,
            },
            handleScroll: false,
            handleScale: false,
          })

          chartRef.current = chart

          igroup("indic:createChart", { width, height })
          ilog("indic:create:options", {
            layout: "macd",
            colors: { textColor: axisTextColor, borderColor: gridLineColor, accentColor },
          })

          const ghostSeries = chart.addSeries(LineSeries, {
            color: "rgba(0,0,0,0)",
            lastValueVisible: false,
            priceLineVisible: false,
            crosshairMarkerVisible: false,
            priceScaleId: "right",
          })

          const histSeries = chart.addSeries(HistogramSeries, {
            priceScaleId: "right",
            base: 0,
            priceFormat: { type: "price", precision: 4, minMove: 0.0001 },
          })
          const macdSeries = chart.addSeries(LineSeries, {
            color: "#22c55e",
            lineWidth: 2,
            priceScaleId: "right",
          })
          const signalSeries = chart.addSeries(LineSeries, {
            color: "#f59e0b",
            lineWidth: 2,
            priceScaleId: "right",
          })

          macdSeries.createPriceLine({
            price: 0,
            color: gridLineColor,
            lineWidth: 1,
            lineStyle: LineStyle.Dotted,
            axisLabelVisible: true,
          })

          macdSeriesRef.current = macdSeries
          signalSeriesRef.current = signalSeries
          histSeriesRef.current = histSeries
          ghostSeriesRef.current = ghostSeries

          if (candles && candles.length) {
            const intervalSec = intervalToSeconds(interval)
            const padBars = GHOST_PAD_BARS
            if (ghostSeriesRef.current && padBars > 0 && Number.isFinite(intervalSec) && intervalSec > 0) {
              const timesSec = Array.from(
                new Set(candles.map((candle) => Math.floor(candle.t / 1000))),
              ).sort((a, b) => a - b)
              const ghostTimes = buildGhostTimes(timesSec, padBars, intervalSec)
              const combined = Array.from(new Set([...ghostTimes, ...timesSec])).sort((a, b) => a - b)
              ghostSeriesRef.current.setData(toWhitespaceData(combined))
            }
            applyMACDData(histSeries, macdSeries, signalSeries, candles, params)
            lastSetDataAtRef.current = performance.now()
            ilog("indic:setData:seed")
          }

          ilog("indic:series:create", {
            macd: !!macdSeriesRef.current,
            signal: !!signalSeriesRef.current,
            hist: !!histSeriesRef.current,
          })

          const handleCrosshair = (param: MouseEventParams<Time>) => {
            ilog("indic:xhair:local", { t: param.time }, 16)
            if (param.time != null && typeof param.time === "number") {
              onCrosshairMove?.({ time: param.time as number })
            } else {
              onCrosshairMove?.({ time: null })
            }
          }

          chart.subscribeCrosshairMove(handleCrosshair)
          crosshairHandlerRef.current = handleCrosshair

          setIsReady(true)
          igroupEnd()
        } else {
          chart.resize(width, height)
        }
      })
    })

    resizeObserver.observe(container)

    return () => {
      resizeObserver.disconnect()
      if (resizeRafRef.current != null) {
        cancelAnimationFrame(resizeRafRef.current)
        resizeRafRef.current = null
      }
      timeSyncCleanupRef.current?.()
      timeSyncCleanupRef.current = null
      linkedChartRef.current = null
      const chart = chartRef.current
      if (chart) {
        if (crosshairHandlerRef.current) {
          chart.unsubscribeCrosshairMove(crosshairHandlerRef.current)
        }
        chart.remove()
      }
      chartRef.current = null
      macdSeriesRef.current = null
      signalSeriesRef.current = null
      histSeriesRef.current = null
      ghostSeriesRef.current = null
      sizeOkRef.current = false
      setIsReady(false)
      onReady?.(null)
    }
  }, [onCrosshairMove, onReady])

  const syncWith = useCallback(
    (priceChart: IChartApi | null) => {
      timeSyncCleanupRef.current?.()
      timeSyncCleanupRef.current = null
      linkedChartRef.current = priceChart

      const local = chartRef.current
      if (!local || !priceChart || !sizeOkRef.current) {
        ilog("indic:sync:init:guard", {
          hasLocal: !!local,
          hasPrice: !!priceChart,
          sizeOk: sizeOkRef.current,
        })
        return
      }

      const unsync = syncTimeScales(priceChart, local)
      timeSyncCleanupRef.current = unsync

      const sourceRange = priceChart.timeScale().getVisibleLogicalRange()
      if (sourceRange) {
        try {
          local
            .timeScale()
            .setVisibleLogicalRange({ from: sourceRange.from, to: sourceRange.to })
        } catch {
          // ignore
        }
      }

      const spacing = priceChart.timeScale().options().barSpacing
      if (typeof spacing === "number") {
        try {
          local.timeScale().applyOptions({ barSpacing: spacing })
        } catch {
          // ignore
        }
      }
    },
    [chartRef, sizeOkRef],
  )


  useEffect(() => {
    if (!isReady || !chartRef.current || !sizeOkRef.current) {
      ilog("indic:setData:guard", {
        isReady,
        chart: !!chartRef.current,
        sizeOk: sizeOkRef.current,
        ms: !!macdSeriesRef.current,
        ss: !!signalSeriesRef.current,
        hs: !!histSeriesRef.current,
      })
      return
    }
    const ms = macdSeriesRef.current
    const ss = signalSeriesRef.current
    const hs = histSeriesRef.current
    const ghostSeries = ghostSeriesRef.current
    if (!ms || !ss || !hs || !ghostSeries) {
      ilog("indic:setData:guard", {
        isReady,
        chart: !!chartRef.current,
        sizeOk: sizeOkRef.current,
        ms: !!ms,
        ss: !!ss,
        hs: !!hs,
        ghost: !!ghostSeries,
      })
      return
    }

    const now = performance.now()
    ilog("indic:setData:call", { dt: (now - lastSetDataAtRef.current).toFixed(1) })
    lastSetDataAtRef.current = now
    const padBars = GHOST_PAD_BARS
    const intervalSec = intervalToSeconds(interval)
    if (padBars > 0 && Number.isFinite(intervalSec) && intervalSec > 0 && candles.length) {
      const timesSec = Array.from(
        new Set(candles.map((candle) => Math.floor(candle.t / 1000))),
      ).sort((a, b) => a - b)
      const ghostTimes = buildGhostTimes(timesSec, padBars, intervalSec)
      const combined = Array.from(new Set([...ghostTimes, ...timesSec])).sort((a, b) => a - b)
      ghostSeries.setData(toWhitespaceData(combined))
    } else {
      ghostSeries.setData([])
    }

    applyMACDData(hs, ms, ss, candles, params)
  }, [candles, interval, isReady, params.fast, params.slow, params.signal])

  useEffect(() => {
    const chart = chartRef.current
    if (!chart || !isReady) return

    const controls: IndicatorChartControls = {
      chart,
      setExternalCrosshair: macdControls.setExternalCrosshair,
      getValuesForTime: macdControls.getValuesForTime,
      syncWith,
      setDataMACD: (data) => {
        macdSeriesRef.current?.setData(data.macd)
        signalSeriesRef.current?.setData(data.signal)
        histSeriesRef.current?.setData(data.hist)
      },
      updateMACD: (point) => {
        ilog("indic:update", point, 8)
        if (point.macd != null && macdSeriesRef.current) {
          macdSeriesRef.current.update({ time: point.time as Time, value: point.macd })
        }
        if (point.signal != null && signalSeriesRef.current) {
          signalSeriesRef.current.update({ time: point.time as Time, value: point.signal })
        }
        if (point.hist != null && histSeriesRef.current) {
          const dataPoint: HistogramData<Time> = {
            time: point.time as Time,
            value: point.hist,
            color: point.hist >= 0 ? POSITIVE_HISTOGRAM_COLOR : NEGATIVE_HISTOGRAM_COLOR,
          }
          histSeriesRef.current.update(dataPoint)
        }
      },
    }

    controlsRef.current = controls
    onReady?.(controls)

    return () => {
      if (controlsRef.current === controls) {
        controlsRef.current = null
      }
    }
  }, [isReady, macdControls.getValuesForTime, macdControls.setExternalCrosshair, onReady, syncWith])

  return <div ref={containerRef} className={className} />
}
