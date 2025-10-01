"use client"

import { create } from "zustand"

type EMAValueMap = Record<number, number | undefined>

interface EMAValuesState {
  emaValues: EMAValueMap
  setEMAValue: (period: number, value?: number) => void
  resetEMAValues: () => void
}

export const useEMAValues = create<EMAValuesState>((set) => ({
  emaValues: {},
  setEMAValue: (period, value) =>
    set((state) => ({ emaValues: { ...state.emaValues, [period]: value } })),
  resetEMAValues: () => set({ emaValues: {} }),
}))
