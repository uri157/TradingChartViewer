"use client"

import { useCallback, useEffect, useMemo, useRef, type MutableRefObject } from "react"
import type {
  HistogramData,
  IChartApi,
  ISeriesApi,
  LineData,
  Time,
} from "lightweight-charts"

import type { CandleDTO } from "@/lib/api/types"
import {
  computeMACD,
  nextMACD,
  cloneMACDState,
  type MACDParams,
  type MACDState,
  type MACDPoint,
} from "@/lib/indicators/macd"

interface SeriesRefs {
  macd: MutableRefObject<ISeriesApi<"Line"> | null>
  signal: MutableRefObject<ISeriesApi<"Line"> | null>
  hist: MutableRefObject<ISeriesApi<"Histogram"> | null>
}

interface UseIndicatorMACDOptions {
  candles: CandleDTO[]
  params: MACDParams
  chartRef: MutableRefObject<IChartApi | null>
  series: SeriesRefs
}

export interface IndicatorValues {
  macd?: number
  signal?: number
  hist?: number
}

export interface IndicatorMACDControls {
  setExternalCrosshair: (time: number | null) => void
  getValuesForTime: (time: number) => IndicatorValues | null
}

const POSITIVE_COLOR = "#22c55e"
const NEGATIVE_COLOR = "#ef4444"

function toSeconds(timestamp: number): number | null {
  if (!Number.isFinite(timestamp)) return null
  const seconds = Math.floor(timestamp / 1000)
  if (!Number.isFinite(seconds)) return null
  return seconds
}

function buildHistogramPoint(time: number, value: number): HistogramData {
  return {
    time: time as Time,
    value,
    color: value >= 0 ? POSITIVE_COLOR : NEGATIVE_COLOR,
  }
}

export function useIndicatorMACD(options: UseIndicatorMACDOptions): IndicatorMACDControls {
  const { candles, params, chartRef, series } = options
  const macdSeriesRef = series.macd
  const signalSeriesRef = series.signal
  const histSeriesRef = series.hist

  const macdPointsRef = useRef<LineData[]>([])
  const signalPointsRef = useRef<LineData[]>([])
  const histPointsRef = useRef<HistogramData[]>([])
  const macdByTimeRef = useRef<Map<number, number>>(new Map())
  const signalByTimeRef = useRef<Map<number, number>>(new Map())
  const histByTimeRef = useRef<Map<number, number>>(new Map())
  const timesArrayRef = useRef<number[]>([])
  const xhairRafRef = useRef<number | null>(null)
  const stateRef = useRef<MACDState | null>(null)
  const stateBeforeLastRef = useRef<MACDState | null>(null)
  const firstTimeRef = useRef<number | null>(null)
  const lastTimeRef = useRef<number | null>(null)
  const lastLengthRef = useRef<number>(0)
  const lastParamsRef = useRef<{ fast: number; slow: number; signal: number } | null>(null)

  const paramsMemo = useMemo(() => {
    const fast = Math.max(1, Math.trunc(params.fast ?? 12))
    const slow = Math.max(fast + 1, Math.trunc(params.slow ?? 26))
    const signal = Math.max(1, Math.trunc(params.signal ?? 9))
    return { fast, slow, signal }
  }, [params.fast, params.slow, params.signal])

  useEffect(() => {
    const macdSeries = macdSeriesRef.current
    const signalSeries = signalSeriesRef.current
    const histSeries = histSeriesRef.current
    const cleanup = () => {
      if (xhairRafRef.current != null) {
        cancelAnimationFrame(xhairRafRef.current)
        xhairRafRef.current = null
      }
    }

    if (!macdSeries || !signalSeries || !histSeries) {
      return cleanup
    }

    const resetAll = () => {
      macdSeries.setData([])
      signalSeries.setData([])
      histSeries.setData([])
      macdPointsRef.current = []
      signalPointsRef.current = []
      histPointsRef.current = []
      macdByTimeRef.current.clear()
      signalByTimeRef.current.clear()
      histByTimeRef.current.clear()
      timesArrayRef.current = []
      stateRef.current = null
      stateBeforeLastRef.current = null
      firstTimeRef.current = null
      lastTimeRef.current = null
      lastLengthRef.current = 0
      lastParamsRef.current = paramsMemo
    }

    const assignFullResult = (
      macd: MACDPoint[],
      signal: MACDPoint[],
      hist: MACDPoint[],
      state: MACDState,
      stateBeforeLast: MACDState | null,
      firstTime: number | null,
      latestTime: number | null,
      totalLength: number,
    ) => {
      const macdPoints: LineData[] = macd.map((point) => ({ time: point.time as Time, value: point.value }))
      const signalPoints: LineData[] = signal.map((point) => ({ time: point.time as Time, value: point.value }))
      const histPoints: HistogramData[] = hist.map((point) => buildHistogramPoint(point.time, point.value))

      macdSeries.setData(macdPoints)
      signalSeries.setData(signalPoints)
      histSeries.setData(histPoints)

      macdPointsRef.current = macdPoints
      signalPointsRef.current = signalPoints
      histPointsRef.current = histPoints
      macdByTimeRef.current.clear()
      signalByTimeRef.current.clear()
      histByTimeRef.current.clear()

      for (const point of macd) {
        if (Number.isFinite(point.value)) {
          macdByTimeRef.current.set(point.time, point.value)
        }
      }
      for (const point of signal) {
        if (Number.isFinite(point.value)) {
          signalByTimeRef.current.set(point.time, point.value)
        }
      }
      for (const point of hist) {
        if (Number.isFinite(point.value)) {
          histByTimeRef.current.set(point.time, point.value)
        }
      }

      timesArrayRef.current = Array.from(
        new Set([
          ...macd.map((point) => point.time),
          ...signal.map((point) => point.time),
          ...hist.map((point) => point.time),
        ]),
      ).sort((a, b) => a - b)

      stateRef.current = cloneMACDState(state)
      stateBeforeLastRef.current = cloneMACDState(stateBeforeLast)
      firstTimeRef.current = firstTime
      lastTimeRef.current = state.lastTime ?? latestTime ?? null
      lastLengthRef.current = totalLength
      lastParamsRef.current = paramsMemo
    }

    const updateLineSeries = (
      action: "append" | "replace",
      seriesRef: MutableRefObject<ISeriesApi<"Line"> | null>,
      pointsRef: MutableRefObject<LineData[]>,
      value: number | null,
      time: number,
    ) => {
      const lineSeries = seriesRef.current
      if (!lineSeries) return

      const buffer = pointsRef.current

      if (value == null) {
        if (action === "replace" && buffer.length) {
          buffer.pop()
          lineSeries.setData([...buffer])
        }
        return
      }

      const point: LineData = { time: time as Time, value }
      if (action === "append") {
        buffer.push(point)
        if (buffer.length === 1) {
          lineSeries.setData([...buffer])
        } else {
          lineSeries.update(point)
        }
      } else {
        if (buffer.length === 0) {
          buffer.push(point)
          lineSeries.setData([...buffer])
        } else {
          buffer[buffer.length - 1] = point
          lineSeries.update(point)
        }
      }
    }

    const updateHistogramSeries = (
      action: "append" | "replace",
      seriesRef: MutableRefObject<ISeriesApi<"Histogram"> | null>,
      pointsRef: MutableRefObject<HistogramData[]>,
      value: number | null,
      time: number,
    ) => {
      const histogramSeries = seriesRef.current
      if (!histogramSeries) return

      const buffer = pointsRef.current

      if (value == null) {
        if (action === "replace" && buffer.length) {
          buffer.pop()
          histogramSeries.setData([...buffer])
        }
        return
      }

      const point = buildHistogramPoint(time, value)
      if (action === "append") {
        buffer.push(point)
        if (buffer.length === 1) {
          histogramSeries.setData([...buffer])
        } else {
          histogramSeries.update(point)
        }
      } else {
        if (buffer.length === 0) {
          buffer.push(point)
          histogramSeries.setData([...buffer])
        } else {
          buffer[buffer.length - 1] = point
          histogramSeries.update(point)
        }
      }
    }

    if (!candles.length) {
      resetAll()
      return cleanup
    }

    const closes: number[] = []
    const times: number[] = []
    for (const candle of candles) {
      const close = Number(candle.c)
      const time = toSeconds(candle.t)
      if (!Number.isFinite(close) || time == null) continue
      closes.push(close)
      times.push(time)
    }

    if (!closes.length || closes.length !== times.length) {
      resetAll()
      return cleanup
    }

    const firstTime = times[0] ?? null
    const latestTime = times[times.length - 1] ?? null
    const previousFirstTime = firstTimeRef.current
    const lengthDiff = closes.length - lastLengthRef.current

    const paramsChanged =
      !lastParamsRef.current ||
      lastParamsRef.current.fast !== paramsMemo.fast ||
      lastParamsRef.current.slow !== paramsMemo.slow ||
      lastParamsRef.current.signal !== paramsMemo.signal

    const datasetShifted = previousFirstTime != null && firstTime != null && firstTime !== previousFirstTime
    const lengthReset = closes.length < lastLengthRef.current
    const jumpChange = Math.abs(lengthDiff) > 1

    const recalcAll = () => {
      const result = computeMACD(closes, times, paramsMemo.fast, paramsMemo.slow, paramsMemo.signal)
      assignFullResult(result.macd, result.signal, result.hist, result.state, result.stateBeforeLast, firstTime, latestTime, closes.length)
    }

    if (paramsChanged || datasetShifted || lengthReset || jumpChange || stateRef.current == null) {
      recalcAll()
      return cleanup
    }

    if (lastTimeRef.current == null) {
      recalcAll()
      return cleanup
    }

    const latestClose = closes[closes.length - 1]
    const prevLastTime = lastTimeRef.current

    let action: "append" | "replace" | null = null
    if (lengthDiff === 1 && latestTime != null && latestTime > prevLastTime) {
      action = "append"
    } else if (lengthDiff === 0 && latestTime === prevLastTime) {
      action = "replace"
    } else {
      recalcAll()
      return
    }

    if (!Number.isFinite(latestClose) || latestTime == null) {
      return cleanup
    }

    if (action === "append") {
      const previousState = cloneMACDState(stateRef.current)
      const result = nextMACD(previousState, latestClose, paramsMemo)
      const newState = result.state
      newState.lastTime = latestTime

      stateBeforeLastRef.current = cloneMACDState(stateRef.current)
      stateRef.current = cloneMACDState(newState)
      lastTimeRef.current = latestTime
      lastLengthRef.current = closes.length
      lastParamsRef.current = paramsMemo

      updateLineSeries("append", macdSeriesRef, macdPointsRef, result.macd, latestTime)
      updateLineSeries("append", signalSeriesRef, signalPointsRef, result.signal, latestTime)
      updateHistogramSeries("append", histSeriesRef, histPointsRef, result.hist, latestTime)

      if (result.macd != null && Number.isFinite(result.macd)) {
        macdByTimeRef.current.set(latestTime, result.macd)
      } else {
        macdByTimeRef.current.delete(latestTime)
      }
      if (result.signal != null && Number.isFinite(result.signal)) {
        signalByTimeRef.current.set(latestTime, result.signal)
      } else {
        signalByTimeRef.current.delete(latestTime)
      }
      if (result.hist != null && Number.isFinite(result.hist)) {
        histByTimeRef.current.set(latestTime, result.hist)
      } else {
        histByTimeRef.current.delete(latestTime)
      }
      if (latestTime != null) {
        const timesArray = timesArrayRef.current
        if (!timesArray.length || timesArray[timesArray.length - 1] !== latestTime) {
          timesArray.push(latestTime)
        }
      }
      return cleanup
    }

    if (action === "replace") {
      const baseState = cloneMACDState(stateBeforeLastRef.current)
      const result = nextMACD(baseState, latestClose, paramsMemo)
      const newState = result.state
      newState.lastTime = latestTime

      stateRef.current = cloneMACDState(newState)
      lastTimeRef.current = latestTime
      lastLengthRef.current = closes.length
      lastParamsRef.current = paramsMemo

      updateLineSeries("replace", macdSeriesRef, macdPointsRef, result.macd, latestTime)
      updateLineSeries("replace", signalSeriesRef, signalPointsRef, result.signal, latestTime)
      updateHistogramSeries("replace", histSeriesRef, histPointsRef, result.hist, latestTime)

      if (result.macd != null && Number.isFinite(result.macd)) {
        macdByTimeRef.current.set(latestTime, result.macd)
      } else {
        macdByTimeRef.current.delete(latestTime)
      }
      if (result.signal != null && Number.isFinite(result.signal)) {
        signalByTimeRef.current.set(latestTime, result.signal)
      } else {
        signalByTimeRef.current.delete(latestTime)
      }
      if (result.hist != null && Number.isFinite(result.hist)) {
        histByTimeRef.current.set(latestTime, result.hist)
      } else {
        histByTimeRef.current.delete(latestTime)
      }
    }

    return cleanup
  }, [candles, paramsMemo, macdSeriesRef, signalSeriesRef, histSeriesRef])

  const findNearestTime = useCallback((t: number): number | null => {
    const arr = timesArrayRef.current
    if (!arr.length || !Number.isFinite(t)) return null

    let lo = 0
    let hi = arr.length - 1

    while (lo <= hi) {
      const mid = (lo + hi) >> 1
      const value = arr[mid]
      if (value === t) return value
      if (value < t) {
        lo = mid + 1
      } else {
        hi = mid - 1
      }
    }

    if (lo >= arr.length) return arr[arr.length - 1] ?? null
    if (hi < 0) return arr[0] ?? null

    return t - arr[hi] <= arr[lo] - t ? arr[hi] : arr[lo]
  }, [])

  const setExternalCrosshair = useCallback(
    (time: number | null) => {
      const chart = chartRef.current
      const macdSeries = macdSeriesRef.current
      if (!chart || !macdSeries) return

      if (xhairRafRef.current != null) {
        cancelAnimationFrame(xhairRafRef.current)
        xhairRafRef.current = null
      }

      if (time == null) {
        try {
          chart.clearCrosshairPosition()
        } catch {}
        return
      }

      xhairRafRef.current = requestAnimationFrame(() => {
        xhairRafRef.current = null

        const chartInstance = chartRef.current
        const macdSeriesInstance = macdSeriesRef.current
        if (!chartInstance || !macdSeriesInstance) return

        if (!Number.isFinite(time)) {
          try {
            chartInstance.clearCrosshairPosition()
          } catch {}
          return
        }

        const hasData =
          timesArrayRef.current.length > 0 &&
          (macdByTimeRef.current.size > 0 ||
            signalByTimeRef.current.size > 0 ||
            histByTimeRef.current.size > 0)

        if (!hasData) {
          try {
            chartInstance.clearCrosshairPosition()
          } catch {}
          return
        }

        const nearestTime =
          macdByTimeRef.current.has(time) ||
          signalByTimeRef.current.has(time) ||
          histByTimeRef.current.has(time)
            ? time
            : findNearestTime(time)

        if (nearestTime == null) {
          try {
            chartInstance.clearCrosshairPosition()
          } catch {}
          return
        }

        const macdValue = macdByTimeRef.current.get(nearestTime)
        const signalValue = signalByTimeRef.current.get(nearestTime)
        const histValue = histByTimeRef.current.get(nearestTime)
        const value = macdValue ?? signalValue ?? histValue ?? 0

        if (!Number.isFinite(value)) {
          try {
            chartInstance.clearCrosshairPosition()
          } catch {}
          return
        }

        try {
          chartInstance.setCrosshairPosition(value, nearestTime as Time, macdSeriesInstance)
        } catch {
          try {
            chartInstance.clearCrosshairPosition()
          } catch {}
        }
      })
    },
    [chartRef, macdSeriesRef, findNearestTime],
  )

  const getValuesForTime = useCallback((time: number) => {
    const macd = macdByTimeRef.current.get(time) ?? null
    const signal = signalByTimeRef.current.get(time) ?? null
    const hist = histByTimeRef.current.get(time) ?? null

    if (macd == null && signal == null && hist == null) {
      return null
    }

    const result: IndicatorValues = {}
    if (macd != null) result.macd = macd
    if (signal != null) result.signal = signal
    if (hist != null) result.hist = hist

    return result
  }, [])

  return {
    setExternalCrosshair,
    getValuesForTime,
  }
}
