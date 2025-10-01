"use client"

import { create } from "zustand"

const STORAGE_KEY = "tv:layout:price-macd:ratio"
export const DEFAULT_PRICE_TO_MACD_RATIO = 0.72

export interface PriceMacdLayoutState {
  ratio: number
  setRatio: (ratio: number) => void
  loadInitial: () => void
}

const clampRatio = (value: number): number => {
  if (!Number.isFinite(value)) {
    return DEFAULT_PRICE_TO_MACD_RATIO
  }
  return Math.min(1, Math.max(0, value))
}

export const usePriceMacdLayout = create<PriceMacdLayoutState>((set, get) => ({
  ratio: DEFAULT_PRICE_TO_MACD_RATIO,
  setRatio: (value) => {
    const next = clampRatio(value)
    set({ ratio: next })
    if (typeof window !== "undefined") {
      try {
        window.localStorage.setItem(STORAGE_KEY, String(next))
      } catch (error) {
        if (process.env.NODE_ENV !== "production") {
          console.warn("Failed to persist price/MACD ratio", error)
        }
      }
    }
  },
  loadInitial: () => {
    if (typeof window === "undefined") {
      return
    }
    const current = get().ratio
    if (current !== DEFAULT_PRICE_TO_MACD_RATIO) {
      return
    }
    try {
      const raw = window.localStorage.getItem(STORAGE_KEY)
      if (!raw) {
        return
      }
      const parsed = Number.parseFloat(raw)
      if (!Number.isFinite(parsed)) {
        return
      }
      set({ ratio: clampRatio(parsed) })
    } catch (error) {
      if (process.env.NODE_ENV !== "production") {
        console.warn("Failed to load price/MACD ratio", error)
      }
    }
  },
}))
