import { clsx, type ClassValue } from 'clsx'
import { twMerge } from 'tailwind-merge'

export function cn(...inputs: ClassValue[]) {
  return twMerge(clsx(inputs))
}

export function cssVarToRgba(varName: string, alpha = 1): string {
  if (typeof window === 'undefined') return `rgba(0,0,0,${alpha})`

  const root = document.documentElement
  const computed = getComputedStyle(root)
  const raw = computed.getPropertyValue(varName).trim()
  if (!raw) return `rgba(0,0,0,${alpha})`

  const probe = document.createElement('div')
  probe.style.color = `hsl(${raw})`
  document.body.appendChild(probe)
  const rgb = getComputedStyle(probe).color
  probe.remove()

  const nums = rgb.match(/\d+/g)?.map(Number) ?? [0, 0, 0]
  const [r, g, b] = nums
  return `rgba(${r}, ${g}, ${b}, ${alpha})`
}

export function resolveCssColor(input: string, alpha = 1): string {
  if (typeof window === 'undefined') return `rgba(0,0,0,${alpha})`
  if (!input) return `rgba(0,0,0,${alpha})`

  const root = document.documentElement
  const computed = getComputedStyle(root)

  let expr = input.trim()

  const hslVar = expr.match(/hsl\(\s*var\((--[^)]+)\)\s*\)/i)
  if (hslVar) {
    const triplet = computed.getPropertyValue(hslVar[1]).trim()
    if (triplet) expr = `hsl(${triplet})`
  } else {
    const onlyVar = expr.match(/var\((--[^)]+)\)/i)
    if (onlyVar) {
      const value = computed.getPropertyValue(onlyVar[1]).trim()
      if (value) expr = value
    }
  }

  const probe = document.createElement('div')
  probe.style.color = expr
  document.body.appendChild(probe)
  const rgb = getComputedStyle(probe).color
  probe.remove()

  const nums = rgb.match(/\d+/g)?.map(Number) ?? [0, 0, 0]
  const [r, g, b] = nums
  return `rgba(${r}, ${g}, ${b}, ${alpha})`
}

export function clamp(value: number, min: number, max: number): number {
  return Math.min(Math.max(value, min), max)
}

export type Interval = '1m' | '5m' | '15m' | '1h' | '4h' | '1d'

export function intervalToSeconds(iv: Interval): number {
  switch (iv) {
    case '1m':
      return 60
    case '5m':
      return 5 * 60
    case '15m':
      return 15 * 60
    case '1h':
      return 60 * 60
    case '4h':
      return 4 * 60 * 60
    case '1d':
      return 24 * 60 * 60
  }
}

export function floorToStep(sec: number, step: number) {
  return Math.floor(sec / step) * step
}

// Convierte cualquier objeto time de lightweight-charts a segundos UNIX (número)
export function timeToUnixSeconds(t: any): number | null {
  if (t == null) return null
  if (typeof t === 'number') return t > 1e12 ? Math.floor(t / 1000) : t
  if (typeof t === 'object') {
    // BusinessDay { year, month, day } → conviértelo a UTC 00:00
    if ('year' in t && 'month' in t && 'day' in t) {
      const ms = Date.UTC((t.year as number) ?? 0, (t.month as number) - 1, t.day as number, 0, 0, 0, 0)
      return Math.floor(ms / 1000)
    }
  }
  return null
}
