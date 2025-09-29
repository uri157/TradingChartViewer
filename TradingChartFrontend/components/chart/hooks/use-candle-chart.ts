"use client"

import {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
  type Dispatch,
  type MutableRefObject,
  type SetStateAction,
} from "react"
import {
  CandlestickSeries,
  ColorType,
  LineSeries,
  LineStyle,
  createChart,
  type CandlestickData,
  type IChartApi,
  type ISeriesApi,
  type MouseEventParams,
  type LineData,
  type Time,
  type LogicalRange,
  type WhitespaceData,
} from "lightweight-charts"

import type { CandleDTO } from "@/lib/api/types"
import { clamp, intervalToSeconds, resolveCssColor, type Interval } from "@/lib/utils"
import { useChartStore } from "@/lib/state/use-chart-store"
import { getEmaColor } from "@/lib/chart/ema"
import { buildGhostTimes, GHOST_PAD_BARS, toWhitespaceData } from "@/lib/chart/ghost"

const isDebug = () => process.env.NEXT_PUBLIC_CHART_DEBUG === "1"

function arrayEquals(a: number[], b: number[]) {
  if (a.length !== b.length) return false
  for (let index = 0; index < a.length; index++) {
    if (a[index] !== b[index]) {
      return false
    }
  }
  return true
}

function normaliseEmaPeriods(periods?: number[] | null): number[] {
  if (!periods?.length) {
    return []
  }

  return periods
    .filter((value) => Number.isFinite(value))
    .map((value) => Math.trunc(value))
    .filter((value) => value > 0)
    .sort((a, b) => a - b)
}

function buildEmaLineData(
  period: number,
  closes: readonly number[],
  times: readonly (number | null)[],
): { points: LineData[]; lastValue: number | null; previousValue: number | null } {
  const emaValues = computeEMA(period, closes as number[])
  const emaPoints: LineData[] = []

  for (let index = 0; index < emaValues.length; index++) {
    const value = emaValues[index]
    const epoch = times[index]

    if (value === undefined || epoch === null) {
      continue
    }

    if (!Number.isFinite(value) || !Number.isFinite(epoch)) {
      continue
    }

    emaPoints.push({ time: epoch as Time, value })
  }

  if (!emaPoints.length) {
    return { points: [], lastValue: null, previousValue: null }
  }

  const lastPoint = emaPoints[emaPoints.length - 1]!
  const previousPoint = emaPoints.length > 1 ? emaPoints[emaPoints.length - 2] : null

  return {
    points: emaPoints,
    lastValue: lastPoint?.value ?? null,
    previousValue: previousPoint?.value ?? null,
  }
}

function nextEMA(prevEMA: number, close: number, alpha: number): number {
  return alpha * close + (1 - alpha) * prevEMA
}

function computeEMA(period: number, closes: number[]): (number | undefined)[] {
  if (!Number.isFinite(period) || period <= 0) {
    return closes.map(() => undefined)
  }

  const alpha = 2 / (period + 1)
  const emaValues: (number | undefined)[] = new Array(closes.length).fill(undefined)
  let ema: number | undefined
  let sum = 0

  for (let index = 0; index < closes.length; index++) {
    const close = closes[index]
    if (!Number.isFinite(close)) {
      emaValues[index] = undefined
      continue
    }

    if (index < period) {
      sum += close
      if (index === period - 1) {
        ema = sum / period
        emaValues[index] = ema
      }
      continue
    }

    if (ema === undefined) {
      continue
    }

    ema = nextEMA(ema, close, alpha)
    emaValues[index] = ema
  }

  return emaValues
}

export interface CandleChartControls {
  chart: IChartApi
  series: ISeriesApi<"Candlestick">
  zoomIn: () => void
  zoomOut: () => void
  goLive: () => void
  moveCrosshair: (time: number | null, price?: number | null) => void
  getLastTime: () => number | null
  appendCandle: (candle: CandleDTO) => void
  replaceLastCandle: (candle: CandleDTO) => void
}

interface UseCandleChartOptions {
  data: CandleDTO[]
  interval: Interval
  onCrosshairMove?: (payload: { price: number | null; time: number | null }) => void
  onReady?: (controls: CandleChartControls | null) => void
}

interface ChartInternals {
  chartRef: MutableRefObject<IChartApi | null>
  seriesRef: MutableRefObject<ISeriesApi<"Candlestick"> | null>
  emaSeriesMapRef: MutableRefObject<Map<number, ISeriesApi<"Line">>>
  emaLastValueRef: MutableRefObject<Map<number, number>>
  emaPreviousValueRef: MutableRefObject<Map<number, number>>
  desiredEmaPeriodsRef: MutableRefObject<number[]>
  formattedClosesRef: MutableRefObject<number[] | null>
  formattedTimesRef: MutableRefObject<(number | null)[] | null>
  ghostSeriesRef: MutableRefObject<ISeriesApi<"Line"> | null>
  containerRef: MutableRefObject<HTMLDivElement | null>
  barSpacingRef: MutableRefObject<number | null>
  followingRef: MutableRefObject<boolean>
  lastDataLengthRef: MutableRefObject<number>
  lastTimeRef: MutableRefObject<number | null>
  firstTimeRef: MutableRefObject<number | null>
  fitDoneRef: MutableRefObject<boolean>
  sizeReadyRef: MutableRefObject<boolean>
  closeByTimeRef: MutableRefObject<Map<number, number>>
  candlestickBufferRef: MutableRefObject<CandlestickData[]>
  ghostBufferRef: MutableRefObject<WhitespaceData<Time>[]>
  intervalRef: MutableRefObject<Interval | null>
  draftOpenByTimeRef: MutableRefObject<Map<number, number>>
}

interface InteractionInternals {
  isDraggingRef: MutableRefObject<boolean>
  dragStartLogicalRef: MutableRefObject<number | null>
}

const MIN_BAR_SPACING = 2
const MAX_BAR_SPACING = 40
const DEFAULT_BAR_SPACING = 8
const ZOOM_RATIO = 1.2
const KEYBOARD_SCROLL_FRACTION = 0.1

function mapCandlesToSeries(candles: CandleDTO[]): CandlestickData[] {
  const formatted: CandlestickData[] = []
  const debug = isDebug()

  for (const candle of candles) {
    const timeSec = Math.floor(candle.t / 1000)
    if (typeof timeSec !== "number" || !Number.isFinite(timeSec)) {
      if (debug) {
        console.warn("[map] dropped candle with invalid time", candle) // Debug: surface malformed candle timestamps.
      }
      continue
    }

    const open = Number(candle.o)
    const high = Number(candle.h)
    const low = Number(candle.l)
    const close = Number(candle.c)

    if (![open, high, low, close].every((value) => Number.isFinite(value))) {
      if (debug) {
        console.warn("[map] dropped candle with invalid price", candle) // Debug: guard against non numeric payloads.
      }
      continue
    }

    const normalisedHigh = Math.max(high, open, close)
    const normalisedLow = Math.min(low, open, close)

    formatted.push({
      time: timeSec as Time,
      open,
      high: normalisedHigh,
      low: normalisedLow,
      close,
    })
  }

  let asc = true
  for (let i = 0; i < formatted.length - 1; i++) {
    if (!(formatted[i].time < formatted[i + 1].time)) {
      asc = false
      break
    }
  }

  if (!asc && formatted.length >= 2) {
    formatted.sort((a, b) => (a.time as number) - (b.time as number))
    asc = true
    if (debug) {
      console.log("[map] sorted asc", formatted.length) // Debug: record corrective sort to keep times ascending.
    }
  }

  if (debug) {
    const firstSec = formatted[0]?.time ?? "n/a"
    const lastSec = formatted[formatted.length - 1]?.time ?? "n/a"
    const timeType = formatted[0] ? typeof formatted[0].time : "undefined"
    console.log(
      `[map] len=${formatted.length} time=sec type=${timeType} first=${firstSec} last=${lastSec} asc=${asc}`,
      // Debug: confirm mapping output before feeding the chart.
    )
  }

  return formatted
}

function mapCandleToSeries(candle: CandleDTO): { data: CandlestickData; time: number } | null {
  const timeSec = Math.floor(candle.t / 1000)
  if (typeof timeSec !== "number" || !Number.isFinite(timeSec)) {
    if (isDebug()) {
      console.warn("[map] dropped candle with invalid time", candle)
    }
    return null
  }

  const open = Number(candle.o)
  const high = Number(candle.h)
  const low = Number(candle.l)
  const close = Number(candle.c)

  if (![open, high, low, close].every((value) => Number.isFinite(value))) {
    if (isDebug()) {
      console.warn("[map] dropped candle with invalid price", candle)
    }
    return null
  }

  const normalisedHigh = Math.max(high, open, close)
  const normalisedLow = Math.min(low, open, close)

  const data: CandlestickData = {
    time: timeSec as Time,
    open,
    high: normalisedHigh,
    low: normalisedLow,
    close,
  }

  return { data, time: timeSec }
}

function useChartInternals(): ChartInternals & InteractionInternals {
  const chartRef = useRef<IChartApi | null>(null)
  const seriesRef = useRef<ISeriesApi<"Candlestick"> | null>(null)
  const emaSeriesMapRef = useRef<Map<number, ISeriesApi<"Line">>>(new Map())
  const emaLastValueRef = useRef<Map<number, number>>(new Map())
  const emaPreviousValueRef = useRef<Map<number, number>>(new Map())
  const desiredEmaPeriodsRef = useRef<number[]>([])
  const formattedClosesRef = useRef<number[] | null>(null)
  const formattedTimesRef = useRef<(number | null)[] | null>(null)
  const ghostSeriesRef = useRef<ISeriesApi<"Line"> | null>(null)
  const containerRef = useRef<HTMLDivElement | null>(null)
  const barSpacingRef = useRef<number | null>(null)
  const followingRef = useRef(true)
  const lastDataLengthRef = useRef(0)
  const lastTimeRef = useRef<number | null>(null)
  const firstTimeRef = useRef<number | null>(null)
  const fitDoneRef = useRef(false)
  const sizeReadyRef = useRef(false)
  const closeByTimeRef = useRef<Map<number, number>>(new Map())
  const candlestickBufferRef = useRef<CandlestickData[]>([])
  const ghostBufferRef = useRef<WhitespaceData<Time>[]>([])
  const intervalRef = useRef<Interval | null>(null)
  const draftOpenByTimeRef = useRef<Map<number, number>>(new Map())
  const isDraggingRef = useRef(false)
  const dragStartLogicalRef = useRef<number | null>(null)

  return {
    chartRef,
    seriesRef,
    emaSeriesMapRef,
    ghostSeriesRef,
    emaLastValueRef,
    emaPreviousValueRef,
    desiredEmaPeriodsRef,
    formattedClosesRef,
    formattedTimesRef,
    containerRef,
    barSpacingRef,
    followingRef,
    lastDataLengthRef,
    lastTimeRef,
    firstTimeRef,
    fitDoneRef,
    sizeReadyRef,
    closeByTimeRef,
    candlestickBufferRef,
    ghostBufferRef,
    intervalRef,
    draftOpenByTimeRef,
    isDraggingRef,
    dragStartLogicalRef,
  }
}

function useBarSpacingControls(
  chartRef: MutableRefObject<IChartApi | null>,
  barSpacingRef: MutableRefObject<number | null>,
  onUserPanOrZoom: () => void,
) {
  const applyBarSpacing = useCallback(
    (nextSpacing: number) => {
      const chart = chartRef.current
      if (!chart) return

      const newSpacing = clamp(nextSpacing, MIN_BAR_SPACING, MAX_BAR_SPACING)
      if (barSpacingRef.current !== null && newSpacing === barSpacingRef.current) return

      barSpacingRef.current = newSpacing
      chart.applyOptions({
        timeScale: { barSpacing: newSpacing },
      })
    },
    [barSpacingRef, chartRef],
  )

  const zoomIn = useCallback(() => {
    onUserPanOrZoom()
    const currentSpacing = barSpacingRef.current ?? DEFAULT_BAR_SPACING
    applyBarSpacing(currentSpacing * ZOOM_RATIO)
  }, [applyBarSpacing, barSpacingRef, onUserPanOrZoom])

  const zoomOut = useCallback(() => {
    onUserPanOrZoom()
    const currentSpacing = barSpacingRef.current ?? DEFAULT_BAR_SPACING
    applyBarSpacing(currentSpacing / ZOOM_RATIO)
  }, [applyBarSpacing, barSpacingRef, onUserPanOrZoom])

  return { applyBarSpacing, zoomIn, zoomOut }
}

function useGoLive(chartRef: MutableRefObject<IChartApi | null>, followingRef: MutableRefObject<boolean>) {
  return useCallback(() => {
    followingRef.current = true
    chartRef.current?.timeScale().scrollToRealTime()
  }, [chartRef, followingRef])
}

function useScrollByFraction(chartRef: MutableRefObject<IChartApi | null>, onUserPanOrZoom: () => void) {
  return useCallback(
    (fraction: number) => {
      const chart = chartRef.current
      if (!chart) return

      const timeScale = chart.timeScale()
      const range = timeScale.getVisibleLogicalRange()
      if (!range) return

      const delta = (range.to - range.from) * fraction
      onUserPanOrZoom()
      timeScale.setVisibleLogicalRange({
        from: range.from + delta,
        to: range.to + delta,
      })
    },
    [chartRef, onUserPanOrZoom],
  )
}

function useChartInitialization(
  internals: ChartInternals,
  interactions: InteractionInternals,
  onCrosshairMove: UseCandleChartOptions["onCrosshairMove"],
  onReady: UseCandleChartOptions["onReady"],
  setChartReady: Dispatch<SetStateAction<boolean>>,
  controls: { zoomIn: () => void; zoomOut: () => void; goLive: () => void },
) {
  const {
    chartRef,
    seriesRef,
    emaSeriesMapRef,
    emaLastValueRef,
    emaPreviousValueRef,
    ghostSeriesRef,
    desiredEmaPeriodsRef,
    formattedClosesRef,
    formattedTimesRef,
    containerRef,
    barSpacingRef,
    followingRef,
    lastTimeRef,
    firstTimeRef,
    lastDataLengthRef,
    fitDoneRef,
    sizeReadyRef,
    closeByTimeRef,
    candlestickBufferRef,
    ghostBufferRef,
    intervalRef,
    draftOpenByTimeRef,
  } = internals

  const onCrosshairMoveRef = useRef(onCrosshairMove)
  useEffect(() => {
    onCrosshairMoveRef.current = onCrosshairMove
  }, [onCrosshairMove])

  const onReadyRef = useRef(onReady)
  useEffect(() => {
    onReadyRef.current = onReady
  }, [onReady])

  const controlsRef = useRef(controls)
  useEffect(() => {
    controlsRef.current = controls
  }, [controls])

  useEffect(() => {
    const container = containerRef.current
    if (!container) return

    const textColor = resolveCssColor("hsl(var(--foreground))")
    const borderColor = resolveCssColor("hsl(var(--border))")
    const accentColor = resolveCssColor("hsl(var(--accent))")

    let crosshairHandler: ((param: MouseEventParams<Time>) => void) | null = null

    const resizeObserver = new ResizeObserver((entries) => {
      const entry = entries[0]
      const width = Math.floor(entry.contentRect.width)
      const height = Math.floor(entry.contentRect.height)
      const valid =
        Number.isFinite(width) &&
        Number.isFinite(height) &&
        width >= 2 &&
        height >= 2

      if (!valid) {
        return
      }

      const existingChart = chartRef.current
      const debug = isDebug()

      if (!existingChart) {
        const chart = createChart(container, {
          width,
          height,
          layout: {
            background: { type: ColorType.Solid, color: "transparent" },
            textColor,
            fontSize: 12,
            fontFamily: "var(--font-geist-sans)",
            attributionLogo: false,
          },
          grid: {
            vertLines: {
              color: borderColor,
              style: LineStyle.Solid,
              visible: true,
            },
            horzLines: {
              color: borderColor,
              style: LineStyle.Solid,
              visible: true,
            },
          },
          crosshair: {
            mode: 1,
            vertLine: {
              color: accentColor,
              width: 1,
              style: LineStyle.Solid,
            },
            horzLine: {
              color: accentColor,
              width: 1,
              style: LineStyle.Solid,
            },
          },
          rightPriceScale: {
            borderColor,
            textColor,
            scaleMargins: {
              top: 0.1,
              bottom: 0.1,
            },
          },
          timeScale: {
            borderColor,
            visible: true,
            borderVisible: true,
            fixLeftEdge: false,
            fixRightEdge: false,
            timeVisible: true,
            secondsVisible: false,
          },
          handleScroll: false,
          handleScale: false,
        })

        const candlestickSeries = chart.addSeries(CandlestickSeries, {
          upColor: "rgba(34,197,94,1)",
          downColor: "rgba(239,68,68,1)",
          borderUpColor: "rgba(34,197,94,1)",
          borderDownColor: "rgba(239,68,68,1)",
          wickUpColor: "rgba(34,197,94,1)",
          wickDownColor: "rgba(239,68,68,1)",
        })

        const ghostSeries = chart.addSeries(LineSeries, {
          color: "rgba(0,0,0,0)",
          lineWidth: 1,
          lastValueVisible: false,
          priceLineVisible: false,
          crosshairMarkerVisible: false,
          priceScaleId: "right",
        })

        chartRef.current = chart
        seriesRef.current = candlestickSeries
        ghostSeriesRef.current = ghostSeries
        sizeReadyRef.current = true
        fitDoneRef.current = false
        followingRef.current = true

        const existingBarSpacing =
          chart.timeScale().options().barSpacing ?? barSpacingRef.current ?? DEFAULT_BAR_SPACING
        const clampedInitialBarSpacing = clamp(existingBarSpacing, MIN_BAR_SPACING, MAX_BAR_SPACING)
        barSpacingRef.current = clampedInitialBarSpacing
        chart.applyOptions({ timeScale: { barSpacing: clampedInitialBarSpacing } })

        chart.applyOptions({
          handleScroll: false,
          handleScale: { axisPressedMouseMove: false, mouseWheel: true, pinch: true },
        })

        const handleCrosshairMove = (param: MouseEventParams<Time>) => {
          const time =
            param.time != null && typeof param.time === "number"
              ? (param.time as number)
              : null

          if (param.point && param.seriesData.size > 0) {
            const seriesData = param.seriesData.get(candlestickSeries)
            if (seriesData && typeof seriesData === "object" && "close" in seriesData) {
              const price = Number(seriesData.close)
              const handler = onCrosshairMoveRef.current
              handler?.({ price: Number.isFinite(price) ? price : null, time })
              return
            }
          }

          onCrosshairMoveRef.current?.({ price: null, time })
        }

        chart.subscribeCrosshairMove(handleCrosshairMove)
        crosshairHandler = handleCrosshairMove

        const moveCrosshair = (time: number | null, price?: number | null) => {
          const currentChart = chartRef.current
          const currentSeries = seriesRef.current
          if (!currentChart || !currentSeries) return

          if (time == null) {
            currentChart.clearCrosshairPosition()
            return
          }

          const fallback = closeByTimeRef.current.get(time)
          const targetPrice = price ?? fallback

          if (targetPrice == null || !Number.isFinite(targetPrice)) {
            currentChart.setCrosshairPosition(0, time as Time, currentSeries)
            return
          }

          currentChart.setCrosshairPosition(targetPrice, time as Time, currentSeries)
        }

        const updateGhostSeriesData = () => {
          const ghostSeries = ghostSeriesRef.current
          const intervalValue = intervalRef.current
          if (!ghostSeries) return

          const padBars = GHOST_PAD_BARS
          if (padBars <= 0 || !intervalValue) {
            ghostSeries.setData([])
            ghostBufferRef.current = []
            return
          }

          const intervalSec = intervalToSeconds(intervalValue)
          const timesRef = formattedTimesRef.current
          if (!Number.isFinite(intervalSec) || intervalSec <= 0 || !timesRef) {
            ghostSeries.setData([])
            ghostBufferRef.current = []
            return
          }

          const validTimes = timesRef.filter((value): value is number => value != null && Number.isFinite(value))
          if (!validTimes.length) {
            ghostSeries.setData([])
            ghostBufferRef.current = []
            return
          }

          const uniqueTimes = Array.from(new Set(validTimes)).sort((a, b) => a - b)
          const ghostTimes = buildGhostTimes(uniqueTimes, GHOST_PAD_BARS, intervalSec)
          const combined = Array.from(new Set([...ghostTimes, ...uniqueTimes])).sort((a, b) => a - b)
          const ghostData = toWhitespaceData(combined)
          ghostSeries.setData(ghostData)
          ghostBufferRef.current = ghostData
        }

        const appendCandle = (candle: CandleDTO) => {
          const series = seriesRef.current
          const chartInstance = chartRef.current
          if (!series || !chartInstance) return

          const mapped = mapCandleToSeries(candle)
          if (!mapped) return

          const { data: candleData, time } = mapped
          const buffer = candlestickBufferRef.current
          const lastBufferItem = buffer[buffer.length - 1]
          const lastTime =
            typeof lastBufferItem?.time === "number" && Number.isFinite(lastBufferItem.time)
              ? (lastBufferItem.time as number)
              : null

          if (lastTime != null && time <= lastTime) {
            return
          }

          buffer.push({ ...candleData })
          series.update(candleData)
          lastTimeRef.current = time
          if (firstTimeRef.current == null) {
            firstTimeRef.current = time
          }
          lastDataLengthRef.current = buffer.length
          draftOpenByTimeRef.current.set(time, candleData.open)
          const close = candleData.close
          if (Number.isFinite(close)) {
            closeByTimeRef.current.set(time, close)
          }

          const closesRef = formattedClosesRef.current ?? []
          if (!formattedClosesRef.current) {
            formattedClosesRef.current = closesRef
          }
          closesRef.push(close)

          const timesRef = formattedTimesRef.current ?? []
          if (!formattedTimesRef.current) {
            formattedTimesRef.current = timesRef
          }
          timesRef.push(time)

          const desiredPeriods = desiredEmaPeriodsRef.current
          desiredPeriods.forEach((period) => {
            const emaSeries = emaSeriesMapRef.current.get(period)
            if (!emaSeries) return
            const prevValue = emaLastValueRef.current.get(period)
            if (prevValue == null) return
            const alpha = 2 / (period + 1)
            const value = nextEMA(prevValue, close, alpha)
            if (!Number.isFinite(value)) return
            emaSeries.update({ time: time as Time, value })
            emaPreviousValueRef.current.set(period, prevValue)
            emaLastValueRef.current.set(period, value)
            if (isDebug()) {
              console.log("[ema] update", period, { action: "append" })
            }
          })

          if (followingRef.current) {
            chartInstance.timeScale().scrollToRealTime()
          }

          updateGhostSeriesData()
        }

        const replaceLastCandle = (candle: CandleDTO) => {
          const series = seriesRef.current
          const chartInstance = chartRef.current
          if (!series || !chartInstance) return

          const mapped = mapCandleToSeries(candle)
          if (!mapped) return

          const { data: candleData, time } = mapped
          const buffer = candlestickBufferRef.current
          if (!buffer.length) return

          const lastIndex = buffer.length - 1
          const previous = buffer[lastIndex]
          const previousTime =
            typeof previous?.time === "number" && Number.isFinite(previous.time)
              ? (previous.time as number)
              : null

          if (previousTime == null || time !== previousTime) {
            return
          }

          buffer[lastIndex] = { ...candleData }
          series.update(candleData)
          lastTimeRef.current = time
          lastDataLengthRef.current = buffer.length
          draftOpenByTimeRef.current.set(time, candleData.open)
          const close = candleData.close
          if (Number.isFinite(close)) {
            closeByTimeRef.current.set(time, close)
          }

          const closesRef = formattedClosesRef.current ?? []
          if (!formattedClosesRef.current) {
            formattedClosesRef.current = closesRef
          }
          if (closesRef.length === buffer.length) {
            closesRef[lastIndex] = close
          } else {
            closesRef.push(close)
          }

          const timesRef = formattedTimesRef.current ?? []
          if (!formattedTimesRef.current) {
            formattedTimesRef.current = timesRef
          }
          if (timesRef.length === buffer.length) {
            timesRef[lastIndex] = time
          } else {
            timesRef.push(time)
          }

          const desiredPeriods = desiredEmaPeriodsRef.current
          desiredPeriods.forEach((period) => {
            const emaSeries = emaSeriesMapRef.current.get(period)
            if (!emaSeries) return
            const baseValue =
              emaPreviousValueRef.current.get(period) ?? emaLastValueRef.current.get(period)
            if (baseValue == null) return
            const alpha = 2 / (period + 1)
            const value = nextEMA(baseValue, close, alpha)
            if (!Number.isFinite(value)) return
            emaSeries.update({ time: time as Time, value })
            emaLastValueRef.current.set(period, value)
            if (isDebug()) {
              console.log("[ema] update", period, { action: "replace" })
            }
          })

          if (followingRef.current) {
            chartInstance.timeScale().scrollToRealTime()
          }

          updateGhostSeriesData()
        }

        const getLastTime = () => {
          const buffer = candlestickBufferRef.current
          if (!buffer.length) return null
          const last = buffer[buffer.length - 1]
          if (!last) return null
          const { time } = last
          return typeof time === "number" && Number.isFinite(time) ? (time as number) : null
        }

        const controlsSnapshot = controlsRef.current
        onReadyRef.current?.({
          chart,
          series: candlestickSeries,
          zoomIn: controlsSnapshot.zoomIn,
          zoomOut: controlsSnapshot.zoomOut,
          goLive: controlsSnapshot.goLive,
          moveCrosshair,
          getLastTime,
          appendCandle,
          replaceLastCandle,
        })

        setChartReady(true)

        if (debug) {
          console.log("[chart] created with size", { width, height })
        }
      } else {
        existingChart.resize(width, height)
      }
    })

    resizeObserver.observe(container)

    return () => {
      resizeObserver.disconnect()

      const chart = chartRef.current

      if (chart && crosshairHandler) {
        chart.unsubscribeCrosshairMove(crosshairHandler)
      }

      onReadyRef.current?.(null)

      for (const [period, series] of Array.from(emaSeriesMapRef.current.entries())) {
        if (!chart || !series) continue
        try {
          chart.removeSeries(series)
          if (isDebug()) {
            console.log("[ema] remove (cleanup)", period)
          }
        } catch (error) {
          if (isDebug()) {
            console.warn("[ema] remove failed during cleanup", period, error)
          }
        }
      }

      emaSeriesMapRef.current.clear()
      emaLastValueRef.current.clear()
      emaPreviousValueRef.current.clear()
      desiredEmaPeriodsRef.current = []
      formattedClosesRef.current = null
      formattedTimesRef.current = null
      closeByTimeRef.current.clear()
      draftOpenByTimeRef.current.clear()
      candlestickBufferRef.current = []
      ghostBufferRef.current = []
      intervalRef.current = null

      const candlestickSeries = seriesRef.current
      if (candlestickSeries && chart) {
        try {
          chart.removeSeries(candlestickSeries)
        } catch (error) {
          if (isDebug()) {
            console.warn("[chart] failed to remove candlestick series", error)
          }
        }
      }

      seriesRef.current = null

      const ghostSeries = ghostSeriesRef.current
      if (ghostSeries && chart) {
        try {
          chart.removeSeries(ghostSeries)
        } catch (error) {
          if (isDebug()) {
            console.warn("[chart] failed to remove ghost series", error)
          }
        }
      }

      ghostSeriesRef.current = null

      if (chart) {
        try {
          chart.remove()
        } catch (error) {
          if (isDebug()) {
            console.warn("[chart] failed to remove instance", error)
          }
        }
      }

      chartRef.current = null
      barSpacingRef.current = null
      sizeReadyRef.current = false
      fitDoneRef.current = false
      firstTimeRef.current = null
      lastTimeRef.current = null
      lastDataLengthRef.current = 0
      followingRef.current = true
      interactions.isDraggingRef.current = false
      interactions.dragStartLogicalRef.current = null

      setChartReady(false)
    }
  }, [
    barSpacingRef,
    chartRef,
    closeByTimeRef,
    containerRef,
    desiredEmaPeriodsRef,
    emaLastValueRef,
    emaPreviousValueRef,
    emaSeriesMapRef,
    fitDoneRef,
    followingRef,
    formattedClosesRef,
    formattedTimesRef,
    interactions.dragStartLogicalRef,
    interactions.isDraggingRef,
    lastDataLengthRef,
    lastTimeRef,
    firstTimeRef,
    setChartReady,
    sizeReadyRef,
  ])
}

function useChartInteractions(
  internals: ChartInternals,
  interactions: InteractionInternals,
  isChartReady: boolean,
  controls: {
    applyBarSpacing: (spacing: number) => void
    onUserPanOrZoom: () => void
    scrollByFraction: (fraction: number) => void
    zoomIn: () => void
    zoomOut: () => void
    goLive: () => void
  },
) {
  const { containerRef, chartRef, barSpacingRef } = internals
  const { isDraggingRef, dragStartLogicalRef } = interactions
  const { applyBarSpacing, onUserPanOrZoom, scrollByFraction, zoomIn, zoomOut, goLive } = controls

  useEffect(() => {
    if (!isChartReady) return

    const container = containerRef.current
    const chart = chartRef.current
    if (!container || !chart) return

    const timeScale = chart.timeScale()

    const handleWheel = (event: WheelEvent) => {
      if (event.ctrlKey || event.metaKey) {
        event.preventDefault()
        const direction = event.deltaY < 0 ? 1 : -1
        const multiplier = direction > 0 ? ZOOM_RATIO : 1 / ZOOM_RATIO
        onUserPanOrZoom()
        const currentSpacing = barSpacingRef.current ?? DEFAULT_BAR_SPACING
        applyBarSpacing(currentSpacing * multiplier)
        return
      }

      const dominantDelta = Math.abs(event.deltaX) > Math.abs(event.deltaY) ? event.deltaX : event.deltaY
      if (dominantDelta === 0) return

      event.preventDefault()

      const range = timeScale.getVisibleLogicalRange()
      if (!range) return

      const spacing = barSpacingRef.current ?? DEFAULT_BAR_SPACING
      onUserPanOrZoom()
      const logicalShift = dominantDelta / spacing
      timeScale.setVisibleLogicalRange({
        from: range.from + logicalShift,
        to: range.to + logicalShift,
      })
    }

    let panActive = false
    let startRange: LogicalRange | null = null
    let startLogicalAtPointer: number | null = null
    let panRaf = 0

    const relativeX = (event: PointerEvent) => {
      const rect = (event.currentTarget as HTMLElement).getBoundingClientRect()
      return event.clientX - rect.left
    }

    const onPointerDown = (event: PointerEvent) => {
      if (event.button !== 0) return

      const start = relativeX(event)
      const visibleRange = timeScale.getVisibleLogicalRange() ?? null
      const logicalAtPointer = timeScale.coordinateToLogical(start) as number | null
      if (!visibleRange || logicalAtPointer == null) {
        return
      }

      panActive = true
      startRange = visibleRange
      startLogicalAtPointer = logicalAtPointer
      isDraggingRef.current = true
      dragStartLogicalRef.current = logicalAtPointer
      onUserPanOrZoom()
      container.setPointerCapture?.(event.pointerId)
    }

    const onPointerMove = (event: PointerEvent) => {
      if (!panActive || !startRange || startLogicalAtPointer == null) {
        return
      }

      if (panRaf) return
      const x = relativeX(event)
      panRaf = requestAnimationFrame(() => {
        panRaf = 0
        const logicalNow = timeScale.coordinateToLogical(x) as number | null
        if (logicalNow == null || !startRange) return

        const deltaBars = logicalNow - startLogicalAtPointer!
        let from = startRange.from - deltaBars
        let to = startRange.to - deltaBars

        timeScale.setVisibleLogicalRange({ from, to })
        dragStartLogicalRef.current = logicalNow
      })
    }

    const endPan = (event: PointerEvent) => {
      if (!panActive) return
      panActive = false
      startRange = null
      startLogicalAtPointer = null
      isDraggingRef.current = false
      dragStartLogicalRef.current = null
      if (panRaf) {
        cancelAnimationFrame(panRaf)
        panRaf = 0
      }
      try {
        container.releasePointerCapture?.(event.pointerId)
      } catch (error) {
        if (isDebug()) {
          console.warn("[chart] releasePointerCapture failed", error)
        }
      }
    }

    const handleKeyDown = (event: KeyboardEvent) => {
      const target = event.target as HTMLElement | null
      if (target && ["INPUT", "TEXTAREA"].includes(target.tagName)) return

      switch (event.key) {
        case "+":
        case "=":
          event.preventDefault()
          zoomIn()
          break
        case "-":
        case "_":
          event.preventDefault()
          zoomOut()
          break
        case "ArrowLeft":
          event.preventDefault()
          scrollByFraction(-KEYBOARD_SCROLL_FRACTION)
          break
        case "ArrowRight":
          event.preventDefault()
          scrollByFraction(KEYBOARD_SCROLL_FRACTION)
          break
        case "Home":
          event.preventDefault()
          goLive()
          break
      }
    }

    container.addEventListener("wheel", handleWheel, { passive: false })
    container.addEventListener("pointerdown", onPointerDown)
    container.addEventListener("pointermove", onPointerMove)
    container.addEventListener("pointerup", endPan)
    container.addEventListener("pointerleave", endPan)
    container.addEventListener("pointercancel", endPan)
    window.addEventListener("keydown", handleKeyDown)

    return () => {
      container.removeEventListener("wheel", handleWheel)
      container.removeEventListener("pointerdown", onPointerDown)
      container.removeEventListener("pointermove", onPointerMove)
      container.removeEventListener("pointerup", endPan)
      container.removeEventListener("pointerleave", endPan)
      container.removeEventListener("pointercancel", endPan)
      window.removeEventListener("keydown", handleKeyDown)
      if (panRaf) {
        cancelAnimationFrame(panRaf)
        panRaf = 0
      }
    }
  }, [
    applyBarSpacing,
    barSpacingRef,
    chartRef,
    containerRef,
    dragStartLogicalRef,
    goLive,
    isChartReady,
    isDraggingRef,
    onUserPanOrZoom,
    scrollByFraction,
    zoomIn,
    zoomOut,
  ])
}

function useSeriesData(
  internals: ChartInternals,
  data: CandleDTO[],
  interval: Interval,
  isChartReady: boolean,
  setIsLoading: Dispatch<SetStateAction<boolean>>,
  emaPeriods: number[],
) {
  const {
    chartRef,
    seriesRef,
    followingRef,
    lastDataLengthRef,
    lastTimeRef,
    firstTimeRef,
    barSpacingRef,
    fitDoneRef,
    sizeReadyRef,
    emaSeriesMapRef,
    emaLastValueRef,
    emaPreviousValueRef,
    closeByTimeRef,
    ghostSeriesRef,
    desiredEmaPeriodsRef,
    formattedClosesRef,
    formattedTimesRef,
    candlestickBufferRef,
    ghostBufferRef,
    intervalRef,
    draftOpenByTimeRef,
  } = internals

  useEffect(() => {
    const chart = chartRef.current
    const series = seriesRef.current
    const debug = isDebug()

    intervalRef.current = interval

    const desiredPeriods = normaliseEmaPeriods(emaPeriods)
    const desiredChanged = !arrayEquals(desiredEmaPeriodsRef.current, desiredPeriods)
    desiredEmaPeriodsRef.current = desiredPeriods

    if (!isChartReady || !sizeReadyRef.current || !series || !chart) {
      if (!data.length) {
        firstTimeRef.current = null
        lastTimeRef.current = null
        fitDoneRef.current = false
        const ghostSeries = ghostSeriesRef.current
        if (ghostSeries) {
          ghostSeries.setData([])
        }
        candlestickBufferRef.current = []
        ghostBufferRef.current = []
      }
      return
    }

    const logEma = (action: string, period: number, details?: unknown) => {
      if (!isDebug()) return
      if (details === undefined) {
        console.log(`[ema] ${action}`, period)
      } else {
        console.log(`[ema] ${action}`, period, details)
      }
    }

    const ensureEmaSeries = (period: number) => {
      let emaSeries = emaSeriesMapRef.current.get(period)
      if (!emaSeries) {
        emaSeries = chart.addSeries(LineSeries, {
          color: getEmaColor(period),
          lineWidth: 2,
          priceScaleId: "right",
        })
        emaSeriesMapRef.current.set(period, emaSeries)
        logEma("add", period)
      }
      return emaSeries
    }

    const removeEmaSeries = (period: number) => {
      const targetSeries = emaSeriesMapRef.current.get(period)
      if (targetSeries && chartRef.current) {
        try {
          chartRef.current.removeSeries(targetSeries)
          logEma("remove", period)
        } catch (error) {
          if (isDebug()) {
            console.warn("[ema] remove failed", period, error)
          }
        }
      }
      emaSeriesMapRef.current.delete(period)
      emaLastValueRef.current.delete(period)
      emaPreviousValueRef.current.delete(period)
    }

    const clearEmaData = (period: number) => {
      const emaSeries = emaSeriesMapRef.current.get(period)
      if (emaSeries) {
        logEma("setData", period, { len: 0 })
        emaSeries.setData([])
      }
      emaLastValueRef.current.delete(period)
      emaPreviousValueRef.current.delete(period)
    }

    const desiredSet = new Set(desiredPeriods)
    for (const period of desiredPeriods) {
      ensureEmaSeries(period)
    }

    for (const [period] of Array.from(emaSeriesMapRef.current.entries())) {
      if (!desiredSet.has(period)) {
        removeEmaSeries(period)
      }
    }

    const ghostSeries = ghostSeriesRef.current
    const intervalSec = intervalToSeconds(interval)
    const padBars = GHOST_PAD_BARS

    if (!data.length) {
      series.setData([])
      desiredPeriods.forEach(clearEmaData)
      formattedClosesRef.current = null
      formattedTimesRef.current = null
      firstTimeRef.current = null
      lastTimeRef.current = null
      lastDataLengthRef.current = 0
      fitDoneRef.current = false
      draftOpenByTimeRef.current.clear()
      closeByTimeRef.current.clear()
      candlestickBufferRef.current = []
      ghostBufferRef.current = []
      if (ghostSeries) {
        ghostSeries.setData([])
      }
      setIsLoading(false)
      return
    }

    const formattedData = mapCandlesToSeries(data)

    if (formattedData.length < 2) {
      if (debug) {
        console.warn(`[chart:setData] skipped len=${formattedData.length}`)
      }
      desiredPeriods.forEach(clearEmaData)
      formattedClosesRef.current = null
      formattedTimesRef.current = null
      firstTimeRef.current = null
      lastTimeRef.current = null
      lastDataLengthRef.current = 0
      fitDoneRef.current = false
      candlestickBufferRef.current = []
      ghostBufferRef.current = []
      setIsLoading(true)
      return
    }

    const closes = formattedData.map((item) => item.close)
    const times = formattedData.map((item) =>
      typeof item.time === "number" && Number.isFinite(item.time)
        ? (item.time as number)
        : null,
    )

    formattedClosesRef.current = closes
    formattedTimesRef.current = times
    candlestickBufferRef.current = formattedData.map((item) => ({ ...item }))

    if (ghostSeries && padBars > 0 && Number.isFinite(intervalSec) && intervalSec > 0) {
      const validTimes = times.filter((value): value is number => value != null && Number.isFinite(value))
      if (validTimes.length) {
        const uniqueTimes = Array.from(new Set(validTimes)).sort((a, b) => a - b)
        const ghostTimes = buildGhostTimes(uniqueTimes, padBars, intervalSec)
        const combined = Array.from(new Set([...ghostTimes, ...uniqueTimes])).sort((a, b) => a - b)
        const ghostData = toWhitespaceData(combined)
        ghostSeries.setData(ghostData)
        ghostBufferRef.current = ghostData
      } else {
        ghostSeries.setData([])
        ghostBufferRef.current = []
      }
    } else if (ghostSeries) {
      ghostSeries.setData([])
      ghostBufferRef.current = []
    }

    const firstTime =
      typeof formattedData[0]?.time === "number" ? (formattedData[0].time as number) : null
    const latestCandle = formattedData[formattedData.length - 1]
    const latestTime =
      typeof latestCandle?.time === "number" && Number.isFinite(latestCandle.time)
        ? (latestCandle.time as number)
        : null

    if (!latestCandle || latestTime == null) {
      if (debug) {
        console.warn("[chart:setData] missing latest candle after mapping", { latestCandle })
      }
      desiredPeriods.forEach(clearEmaData)
      lastDataLengthRef.current = formattedData.length
      setIsLoading(false)
      return
    }

    const timeScale = chart.timeScale()
    const currentBarSpacing = barSpacingRef.current

    const datasetChanged =
      firstTimeRef.current !== firstTime || formattedData.length < lastDataLengthRef.current

    if (datasetChanged) {
      fitDoneRef.current = false
    }

    const shouldResetSeries = lastDataLengthRef.current === 0 || datasetChanged

    const recomputeAllEmaSeries = () => {
      const closesRef = formattedClosesRef.current
      const timesRef = formattedTimesRef.current
      if (!closesRef || !timesRef) return

      desiredPeriods.forEach((period) => {
        const emaSeries = ensureEmaSeries(period)
        if (!emaSeries) return
        const { points, lastValue, previousValue } = buildEmaLineData(period, closesRef, timesRef)
        logEma("setData", period, { len: points.length })
        emaSeries.setData(points)
        if (points.length && lastValue != null) {
          emaLastValueRef.current.set(period, lastValue)
        } else {
          emaLastValueRef.current.delete(period)
        }
        if (points.length && previousValue != null) {
          emaPreviousValueRef.current.set(period, previousValue)
        } else {
          emaPreviousValueRef.current.delete(period)
        }
      })
    }

    if (shouldResetSeries) {
      if (debug) {
        console.log("[chart] setData prepare", {
          len: formattedData.length,
          first: firstTime,
          last: latestTime,
        })
      }
      const shouldFitAfterSet = !fitDoneRef.current
      draftOpenByTimeRef.current.clear()
      closeByTimeRef.current.clear()
      for (const candle of formattedData) {
        const candleTime = typeof candle.time === "number" ? candle.time : null
        if (candleTime !== null) {
          draftOpenByTimeRef.current.set(candleTime, candle.open)
          closeByTimeRef.current.set(candleTime, candle.close)
        }
      }

      series.setData(formattedData)

      if (typeof currentBarSpacing === "number") {
        chart.applyOptions({ timeScale: { barSpacing: currentBarSpacing } })
      }

      if (shouldFitAfterSet) {
        chart.timeScale().fitContent()
        fitDoneRef.current = true
        if (debug) {
          console.log("[chart] setData + fitContent", { len: formattedData.length })
        }
      } else if (debug) {
        console.log("[chart] setData (no fit)", { len: formattedData.length })
      }

      recomputeAllEmaSeries()

      firstTimeRef.current = typeof firstTime === "number" ? firstTime : null
      lastTimeRef.current = typeof latestTime === "number" ? latestTime : null
      lastDataLengthRef.current = formattedData.length

      setIsLoading(false)
      return
    }

    if (desiredChanged) {
      recomputeAllEmaSeries()
    }

    const lastTime = lastTimeRef.current
    if (lastTime === null) {
      return
    }

    let action: "append" | "in-place" | "stale/ignored"
    if (latestTime < lastTime) {
      action = "stale/ignored"
    } else if (latestTime === lastTime) {
      action = "in-place"
    } else {
      action = "append"
    }

    if (debug) {
      console.log(
        `[chart:update] latest=${latestTime} lastRef=${lastTime} action=${action}`,
      )
    }

    if (action === "stale/ignored") {
      setIsLoading(false)
      return
    }

    const latestClose =
      typeof latestCandle.close === "number" ? latestCandle.close : Number(latestCandle.close)

    if (action === "in-place") {
      const preservedOpen =
        draftOpenByTimeRef.current.get(latestTime) ??
        (typeof latestCandle.open === "number" ? latestCandle.open : NaN)
      const validOpen = Number.isFinite(preservedOpen) ? preservedOpen : latestCandle.open
      draftOpenByTimeRef.current.set(latestTime, validOpen)
      series.update({ ...latestCandle, open: validOpen })
      if (Number.isFinite(latestClose)) {
        closeByTimeRef.current.set(latestTime, latestClose)
      }
    } else {
      draftOpenByTimeRef.current.set(latestTime, latestCandle.open)
      series.update(latestCandle)
      if (Number.isFinite(latestClose)) {
        closeByTimeRef.current.set(latestTime, latestClose)
      }
    }

    if (action === "in-place") {
      desiredPeriods.forEach((period) => {
        const emaSeries = emaSeriesMapRef.current.get(period)
        if (!emaSeries) return
        const baseValue =
          emaPreviousValueRef.current.get(period) ?? emaLastValueRef.current.get(period)
        if (baseValue == null) return
        const alpha = 2 / (period + 1)
        const value = nextEMA(baseValue, latestClose, alpha)
        if (!Number.isFinite(value)) return
        emaSeries.update({ time: latestTime as Time, value })
        emaLastValueRef.current.set(period, value)
        logEma("update", period, { action })
      })
    } else {
      desiredPeriods.forEach((period) => {
        const emaSeries = emaSeriesMapRef.current.get(period)
        if (!emaSeries) return
        const prevValue = emaLastValueRef.current.get(period)
        if (prevValue == null) return
        const alpha = 2 / (period + 1)
        const value = nextEMA(prevValue, latestClose, alpha)
        if (!Number.isFinite(value)) return
        emaSeries.update({ time: latestTime as Time, value })
        emaPreviousValueRef.current.set(period, prevValue)
        emaLastValueRef.current.set(period, value)
        logEma("update", period, { action })
      })
    }

    lastDataLengthRef.current = formattedData.length
    lastTimeRef.current = latestTime
    if (typeof firstTime === "number") {
      firstTimeRef.current = firstTime
    }

    if (followingRef.current) {
      timeScale.scrollToRealTime()
    }

    setIsLoading(false)
  }, [
    barSpacingRef,
    chartRef,
    closeByTimeRef,
    data,
    desiredEmaPeriodsRef,
    emaLastValueRef,
    emaPeriods,
    emaPreviousValueRef,
    emaSeriesMapRef,
    fitDoneRef,
    followingRef,
    firstTimeRef,
    formattedClosesRef,
    formattedTimesRef,
    interval,
    isChartReady,
    lastDataLengthRef,
    lastTimeRef,
    seriesRef,
    setIsLoading,
    sizeReadyRef,
  ])
}

export function useCandleChart(options: UseCandleChartOptions) {
  const { data, interval, onCrosshairMove, onReady } = options
  const [isLoading, setIsLoading] = useState(true)
  const [isChartReady, setIsChartReady] = useState(false)
  const emaPeriods = useChartStore((state) => state.emaPeriods)

  const internals = useChartInternals()
  const { chartRef, barSpacingRef, followingRef } = internals

  const interactions = useMemo(() => {
    return {
      isDraggingRef: internals.isDraggingRef,
      dragStartLogicalRef: internals.dragStartLogicalRef,
    }
  }, [internals.dragStartLogicalRef, internals.isDraggingRef])

  const onUserPanOrZoom = useCallback(() => {
    followingRef.current = false
  }, [followingRef])

  const { applyBarSpacing, zoomIn, zoomOut } = useBarSpacingControls(chartRef, barSpacingRef, onUserPanOrZoom)
  const goLive = useGoLive(chartRef, followingRef)
  const scrollByFraction = useScrollByFraction(chartRef, onUserPanOrZoom)

  useChartInitialization(internals, interactions, onCrosshairMove, onReady, setIsChartReady, {
    zoomIn,
    zoomOut,
    goLive,
  })

  useChartInteractions(internals, interactions, isChartReady, {
    applyBarSpacing,
    onUserPanOrZoom,
    scrollByFraction,
    zoomIn,
    zoomOut,
    goLive,
  })

  useSeriesData(internals, data, interval, isChartReady, setIsLoading, emaPeriods)

  return {
    containerRef: internals.containerRef,
    isLoading,
  }
}
