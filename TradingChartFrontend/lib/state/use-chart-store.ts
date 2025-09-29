"use client"

import { create } from "zustand"
import { persist } from "zustand/middleware"

import type { Symbol, Interval } from "@/lib/api/types"

interface MACDIndicatorState {
  fast: number
  slow: number
  signal: number
  visible: boolean
  heightPx: number
}

interface ChartIndicatorsState {
  macd: MACDIndicatorState
}

interface ChartState {
  symbol: Symbol
  interval: Interval
  range: {
    from?: number
    to?: number
  }
  emaPeriods: number[]
  indicators: ChartIndicatorsState
  setSymbol: (symbol: Symbol) => void
  setInterval: (interval: Interval) => void
  setRange: (range: { from?: number; to?: number }) => void
  addEma: (period: number) => void
  removeEma: (period: number) => void
  showMACD: () => void
  hideMACD: () => void
  toggleMACD: () => void
  setMACDParams: (params: { fast?: number; slow?: number; signal?: number }) => void
  setMACDHeight: (height: number) => void
}

const DEFAULT_MACD_STATE: MACDIndicatorState = {
  fast: 12,
  slow: 26,
  signal: 9,
  visible: false,
  heightPx: 180,
}

export const useChartStore = create<ChartState>()(
  persist(
    (set, get) => ({
      symbol: "BTCUSDT",
      interval: "1m",
      range: {},
      emaPeriods: [9, 21],
      indicators: { macd: { ...DEFAULT_MACD_STATE } },
      setSymbol: (symbol) => set({ symbol }),
      setInterval: (interval) => set({ interval }),
      setRange: (range) => set({ range }),
      addEma: (period) =>
        set((state) => {
          const normalized = Math.trunc(period)
          if (!Number.isFinite(normalized) || normalized <= 0) {
            return state
          }
          if (state.emaPeriods.includes(normalized)) {
            return state
          }
          const next = [...state.emaPeriods, normalized].sort((a, b) => a - b)
          return { emaPeriods: next }
        }),
      removeEma: (period) =>
        set((state) => ({
          emaPeriods: state.emaPeriods.filter((value) => value !== period),
        })),
      showMACD: () =>
        set((state) => ({
          indicators: {
            ...state.indicators,
            macd: { ...state.indicators.macd, visible: true },
          },
        })),
      hideMACD: () =>
        set((state) => ({
          indicators: {
            ...state.indicators,
            macd: { ...state.indicators.macd, visible: false },
          },
        })),
      toggleMACD: () => {
        const {
          indicators: { macd },
        } = get()
        set((state) => ({
          indicators: {
            ...state.indicators,
            macd: { ...macd, visible: !macd.visible },
          },
        }))
      },
      setMACDParams: (params) =>
        set((state) => {
          const requestedFast =
            params.fast !== undefined && Number.isFinite(params.fast) && params.fast > 0
              ? Math.trunc(params.fast)
              : state.indicators.macd.fast
          const requestedSlow =
            params.slow !== undefined && Number.isFinite(params.slow) && params.slow > 0
              ? Math.trunc(params.slow)
              : state.indicators.macd.slow
          const requestedSignal =
            params.signal !== undefined && Number.isFinite(params.signal) && params.signal > 0
              ? Math.trunc(params.signal)
              : state.indicators.macd.signal

          const fast = Math.max(1, requestedFast)
          const slow = Math.max(fast + 1, requestedSlow)
          const signal = Math.max(1, requestedSignal)
          return {
            indicators: {
              ...state.indicators,
              macd: {
                ...state.indicators.macd,
                fast,
                slow,
                signal,
              },
            },
          }
        }),
      setMACDHeight: (height) =>
        set((state) => {
          const minHeight = 120
          const maxHeight = Math.max(minHeight, Math.floor((typeof window !== "undefined" ? window.innerHeight : 600) * 0.5))
          const nextHeight = Math.min(Math.max(Math.floor(height), minHeight), maxHeight)
          return {
            indicators: {
              ...state.indicators,
              macd: { ...state.indicators.macd, heightPx: nextHeight },
            },
          }
        }),
    }),
    {
      name: "chart-store",
      partialize: (state) => ({
        symbol: state.symbol,
        interval: state.interval,
        range: state.range,
        emaPeriods: state.emaPeriods,
        indicators: state.indicators,
      }),
    },
  ),
)
