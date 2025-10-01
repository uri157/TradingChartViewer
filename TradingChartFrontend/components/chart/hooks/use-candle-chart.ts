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
  type ITimeScaleApi,
  type MouseEventParams,
  type LineData,
  type Time,
  type LogicalRange,
  type WhitespaceData,
} from "lightweight-charts"

import type { CandleDTO } from "@/lib/api/types"
import { clamp, intervalToSeconds, resolveCssColor, type Interval } from "@/lib/utils"
import { useChartStore } from "@/lib/state/use-chart-store"
import { useEMAValues } from "@/lib/state/use-ema-values"
import { getEmaColor } from "@/lib/chart/ema"
import { buildGhostTimes, GHOST_PAD_BARS, toWhitespaceData } from "@/lib/chart/ghost"
import { BASE_TIME_SCALE_OPTIONS } from "@/lib/chart/sync"

const isDebug = () => process.env.NEXT_PUBLIC_CHART_DEBUG === "1"

function isAtRightEdge(timeScale: ITimeScaleApi) {
  const position = timeScale.scrollPosition()
  return position !== null && position > -0.5 && position < 0.5
}

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
  onRequestAutoStick?: (chart: IChartApi) => void
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
  autoStickRef: MutableRefObject<boolean>
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
const INITIAL_VISIBLE_BARS = 30

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
  const autoStickRef = useRef(true)
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
    autoStickRef,
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
  const zoomIn = useCallback(() => {
    onUserPanOrZoom()
    const currentSpacing = barSpacingRef.current ?? DEFAULT_BAR_SPACING
    const nextSpacing = clamp(currentSpacing * ZOOM_RATIO, MIN_BAR_SPACING, MAX_BAR_SPACING)
    barSpacingRef.current = nextSpacing
    useChartStore.getState().zoomIn()
  }, [barSpacingRef, onUserPanOrZoom])

  const zoomOut = useCallback(() => {
    onUserPanOrZoom()
    const currentSpacing = barSpacingRef.current ?? DEFAULT_BAR_SPACING
    const nextSpacing = clamp(currentSpacing / ZOOM_RATIO, MIN_BAR_SPACING, MAX_BAR_SPACING)
    barSpacingRef.current = nextSpacing
    useChartStore.getState().zoomOut()
  }, [barSpacingRef, onUserPanOrZoom])

  return { zoomIn, zoomOut }
}

function useGoLive(
  chartRef: MutableRefObject<IChartApi | null>,
  autoStickRef: MutableRefObject<boolean>,
  onRequestAutoStick?: (chart: IChartApi) => void,
) {
  return useCallback(() => {
    autoStickRef.current = true
    const chart = chartRef.current
    if (!chart) return
    if (onRequestAutoStick) {
      onRequestAutoStick(chart)
    } else {
      chart.timeScale().scrollToRealTime()
    }
  }, [autoStickRef, chartRef, onRequestAutoStick])
}

function useChartInitialization(
  internals: ChartInternals,
  interactions: InteractionInternals,
  onCrosshairMove: UseCandleChartOptions["onCrosshairMove"],
  onReady: UseCandleChartOptions["onReady"],
  setChartReady: Dispatch<SetStateAction<boolean>>,
  controls: { zoomIn: () => void; zoomOut: () => void; goLive: () => void },
  registerChart: (chart: IChartApi | null) => void,
  onRequestAutoStick?: (chart: IChartApi) => void,
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
    autoStickRef,
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

  const setEMAValue = useEMAValues((state) => state.setEMAValue)
  const resetEMAValues = useEMAValues((state) => state.resetEMAValues)

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

    const axisTextColor = resolveCssColor("var(--chart-axis-text)")
    const gridLineColor = resolveCssColor("var(--chart-grid-line)")
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
            textColor: axisTextColor,
            fontSize: 12,
            fontFamily: "var(--font-geist-sans)",
            attributionLogo: false,
          },
          grid: {
            vertLines: {
              color: gridLineColor,
              style: LineStyle.Solid,
              visible: true,
            },
            horzLines: {
              color: gridLineColor,
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
            borderColor: gridLineColor,
            textColor: axisTextColor,
            scaleMargins: {
              top: 0.1,
              bottom: 0.1,
            },
          },
          timeScale: {
            ...BASE_TIME_SCALE_OPTIONS,
            borderColor: gridLineColor,
            visible: true,
            borderVisible: true,
            fixLeftEdge: false,
            timeVisible: true,
            secondsVisible: false,
          },
          handleScroll: {
            mouseWheel: true,
            pressedMouseMove: true,
            horzTouchDrag: true,
            vertTouchDrag: true,
          },
          handleScale: {
            mouseWheel: true,
            pinch: true,
            axisPressedMouseMove: {
              time: true,
              price: true,
            },
          },
        })

        const candlestickSeries = chart.addSeries(CandlestickSeries, {
          upColor: "rgba(34,197,94,1)",
          downColor: "rgba(239,68,68,1)",
          borderUpColor: "rgba(34,197,94,1)",
          borderDownColor: "rgba(239,68,68,1)",
          wickUpColor: "rgba(34,197,94,1)",
          wickDownColor: "rgba(239,68,68,1)",
        })

        candlestickSeries.priceScale().applyOptions({
          autoScale: false,
          scaleMargins: {
            top: 0.15,
            bottom: 0.2,
          },
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
        registerChart(chart)
        sizeReadyRef.current = true
        fitDoneRef.current = false
        autoStickRef.current = true

        const existingBarSpacing =
          chart.timeScale().options().barSpacing ?? barSpacingRef.current ?? DEFAULT_BAR_SPACING
        const clampedInitialBarSpacing = clamp(existingBarSpacing, MIN_BAR_SPACING, MAX_BAR_SPACING)
        barSpacingRef.current = clampedInitialBarSpacing

        chart.applyOptions({
          timeScale: {
            ...BASE_TIME_SCALE_OPTIONS,
            barSpacing: clampedInitialBarSpacing,
          },
          handleScroll: {
            mouseWheel: true,
            pressedMouseMove: true,
            horzTouchDrag: true,
            vertTouchDrag: true,
          },
          handleScale: {
            mouseWheel: true,
            pinch: true,
            axisPressedMouseMove: {
              time: true,
              price: true,
            },
          },
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
            setEMAValue(period, value)
            if (isDebug()) {
              console.log("[ema] update", period, { action: "append" })
            }
          })

          const timeScale = chartInstance.timeScale()
          if (autoStickRef.current && isAtRightEdge(timeScale)) {
            if (onRequestAutoStick) {
              onRequestAutoStick(chartInstance)
            } else {
              timeScale.scrollToRealTime()
            }
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
            setEMAValue(period, value)
            if (isDebug()) {
              console.log("[ema] update", period, { action: "replace" })
            }
          })

          const timeScale = chartInstance.timeScale()
          if (autoStickRef.current && isAtRightEdge(timeScale)) {
            if (onRequestAutoStick) {
              onRequestAutoStick(chartInstance)
            } else {
              timeScale.scrollToRealTime()
            }
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
      autoStickRef.current = true
      interactions.isDraggingRef.current = false
      interactions.dragStartLogicalRef.current = null

      resetEMAValues()

      registerChart(null)
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
    autoStickRef,
    formattedClosesRef,
    formattedTimesRef,
    interactions.dragStartLogicalRef,
    interactions.isDraggingRef,
    lastDataLengthRef,
    lastTimeRef,
    firstTimeRef,
    setChartReady,
    registerChart,
    sizeReadyRef,
    resetEMAValues,
    setEMAValue,
  ])
}

function useChartInteractions(
  internals: ChartInternals,
  isChartReady: boolean,
  controls: {
    onUserPanOrZoom: () => void
    zoomIn: () => void
    zoomOut: () => void
    goLive: () => void
  },
) {
  const { containerRef, chartRef } = internals
  const { onUserPanOrZoom, zoomIn, zoomOut, goLive } = controls

  useEffect(() => {
    if (!isChartReady) return

    const container = containerRef.current
    const chart = chartRef.current
    if (!container || !chart) return

    const handleKeyDown = (event: KeyboardEvent) => {
      const target = event.target as HTMLElement | null
      if (
        target &&
        (["INPUT", "TEXTAREA"].includes(target.tagName) || target.isContentEditable || event.metaKey || event.ctrlKey)
      ) {
        return
      }

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
        case "Home":
          event.preventDefault()
          goLive()
          break
      }
    }

    const markInteraction = () => {
      onUserPanOrZoom()
    }

    const markInteractionPassive = () => {
      onUserPanOrZoom()
    }

    container.addEventListener("mousedown", markInteraction)
    container.addEventListener("wheel", markInteractionPassive, { passive: true })
    container.addEventListener("touchstart", markInteractionPassive, { passive: true })
    window.addEventListener("keydown", handleKeyDown)

    return () => {
      container.removeEventListener("mousedown", markInteraction)
      container.removeEventListener("wheel", markInteractionPassive)
      container.removeEventListener("touchstart", markInteractionPassive)
      window.removeEventListener("keydown", handleKeyDown)
    }
  }, [chartRef, containerRef, goLive, isChartReady, onUserPanOrZoom, zoomIn, zoomOut])
}

function useSeriesData(
  internals: ChartInternals,
  data: CandleDTO[],
  interval: Interval,
  isChartReady: boolean,
  setIsLoading: Dispatch<SetStateAction<boolean>>,
  emaPeriods: number[],
  onRequestAutoStick?: (chart: IChartApi) => void,
) {
  const {
    chartRef,
    seriesRef,
    autoStickRef,
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

  const setEMAValue = useEMAValues((state) => state.setEMAValue)
  const resetEMAValues = useEMAValues((state) => state.resetEMAValues)

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
          lastValueVisible: false,
          priceLineVisible: false,
          crosshairMarkerVisible: false,
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
      setEMAValue(period, undefined)
    }

    const clearEmaData = (period: number) => {
      const emaSeries = emaSeriesMapRef.current.get(period)
      if (emaSeries) {
        logEma("setData", period, { len: 0 })
        emaSeries.setData([])
      }
      emaLastValueRef.current.delete(period)
      emaPreviousValueRef.current.delete(period)
      setEMAValue(period, undefined)
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
      autoStickRef.current = true
      draftOpenByTimeRef.current.clear()
      closeByTimeRef.current.clear()
      candlestickBufferRef.current = []
      ghostBufferRef.current = []
      if (ghostSeries) {
        ghostSeries.setData([])
      }
      resetEMAValues()
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
      autoStickRef.current = true
      candlestickBufferRef.current = []
      ghostBufferRef.current = []
      resetEMAValues()
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
      autoStickRef.current = true
      resetEMAValues()
    }

    const applyInitialVisibleRange = () => {
      if (!autoStickRef.current) {
        return
      }

      const totalBars = formattedData.length
      if (totalBars === 0) return

      const toIndex = totalBars - 1
      const fromIndex = Math.max(0, toIndex - (INITIAL_VISIBLE_BARS - 1))
      const logicalRange: LogicalRange = {
        from: fromIndex - 0.25,
        to: toIndex + 0.75,
      }

      timeScale.setVisibleLogicalRange(logicalRange)
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
          setEMAValue(period, lastValue)
        } else {
          emaLastValueRef.current.delete(period)
          setEMAValue(period, undefined)
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

      resetEMAValues()
      series.setData(formattedData)

      if (typeof currentBarSpacing === "number") {
        chart.applyOptions({ timeScale: { barSpacing: currentBarSpacing } })
      }

      if (shouldFitAfterSet) {
        let appliedInitialRange = false
        if (autoStickRef.current) {
          applyInitialVisibleRange()
          appliedInitialRange = true
        } else {
          chart.timeScale().fitContent()
        }
        fitDoneRef.current = true
        if (debug) {
          console.log(
            `[chart] setData + ${appliedInitialRange ? "initialRange" : "fitContent"}`,
            { len: formattedData.length },
          )
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
        setEMAValue(period, value)
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
        setEMAValue(period, value)
        logEma("update", period, { action })
      })
    }

    lastDataLengthRef.current = formattedData.length
    lastTimeRef.current = latestTime
    if (typeof firstTime === "number") {
      firstTimeRef.current = firstTime
    }

    if (autoStickRef.current && isAtRightEdge(timeScale)) {
      if (onRequestAutoStick && chart) {
        onRequestAutoStick(chart)
      } else {
        timeScale.scrollToRealTime()
      }
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
    autoStickRef,
    firstTimeRef,
    formattedClosesRef,
    formattedTimesRef,
    onRequestAutoStick,
    interval,
    isChartReady,
    lastDataLengthRef,
    lastTimeRef,
    seriesRef,
    setIsLoading,
    sizeReadyRef,
    resetEMAValues,
    setEMAValue,
  ])
}

export function useCandleChart(options: UseCandleChartOptions) {
  const { data, interval, onCrosshairMove, onReady, onRequestAutoStick } = options
  const [isLoading, setIsLoading] = useState(true)
  const [isChartReady, setIsChartReady] = useState(false)
  const emaPeriods = useChartStore((state) => state.emaPeriods)
  const symbol = useChartStore((state) => state.symbol)
  const setChart = useChartStore((state) => state.setChart)
  const resetEMAValues = useEMAValues((state) => state.resetEMAValues)

  const internals = useChartInternals()
  const { chartRef, barSpacingRef, autoStickRef } = internals

  const interactions = useMemo(() => {
    return {
      isDraggingRef: internals.isDraggingRef,
      dragStartLogicalRef: internals.dragStartLogicalRef,
    }
  }, [internals.dragStartLogicalRef, internals.isDraggingRef])

  const onUserPanOrZoom = useCallback(() => {
    autoStickRef.current = false
  }, [autoStickRef])

  const { zoomIn, zoomOut } = useBarSpacingControls(chartRef, barSpacingRef, onUserPanOrZoom)
  const goLive = useGoLive(chartRef, autoStickRef, onRequestAutoStick)

  useEffect(() => {
    resetEMAValues()
  }, [interval, symbol, resetEMAValues])

  useEffect(() => {
    autoStickRef.current = true
  }, [autoStickRef, interval, symbol])

  useChartInitialization(
    internals,
    interactions,
    onCrosshairMove,
    onReady,
    setIsChartReady,
    {
      zoomIn,
      zoomOut,
      goLive,
    },
    setChart,
    onRequestAutoStick,
  )

  useChartInteractions(internals, isChartReady, {
    onUserPanOrZoom,
    zoomIn,
    zoomOut,
    goLive,
  })

  useSeriesData(internals, data, interval, isChartReady, setIsLoading, emaPeriods, onRequestAutoStick)

  return {
    containerRef: internals.containerRef,
    isLoading,
  }
}
