"use client"

import type React from "react"
import { useCallback, useEffect, useMemo, useRef, useState } from "react"
import { Search, ZoomIn, ZoomOut, RotateCcw, X } from "lucide-react"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select"
import { Separator } from "@/components/ui/separator"
import { Badge } from "@/components/ui/badge"
import { useChartStore } from "@/lib/state/use-chart-store"
import { cn } from "@/lib/utils"
import type { Interval } from "@/lib/api/types"
import { getEmaColor } from "@/lib/chart/ema"
import { Switch } from "@/components/ui/switch"
import { useSymbolIntervals } from "@/lib/api/queries"

interface TopBarProps {
  className?: string
  children?: React.ReactNode
}

const DEFAULT_INTERVALS: { value: Interval; label: string }[] = [
  { value: "1m", label: "1m" },
  { value: "5m", label: "5m" },
  { value: "1h", label: "1h" },
  { value: "1d", label: "1d" },
]

function normalizeIntervals(
  response: ReturnType<typeof useSymbolIntervals>["data"],
): { value: Interval; label: string }[] {
  if (!response) {
    return DEFAULT_INTERVALS
  }

  const { intervals } = response

  if (!Array.isArray(intervals) || intervals.length === 0) {
    return DEFAULT_INTERVALS
  }

  const first = intervals[0]

  if (typeof first === "string") {
    return (intervals as string[]).map((value) => ({ value: value as Interval, label: value }))
  }

  return (intervals as Array<{ name: string }>).map((item) => ({
    value: item.name as Interval,
    label: item.name,
  }))
}

export function TopBar({ className, children }: TopBarProps) {
  const {
    symbol,
    interval,
    emaPeriods,
    indicators,
    setSymbol,
    setInterval,
    addEma,
    removeEma,
    toggleMACD,
    setMACDParams,
  } = useChartStore()
  const { data: symbolIntervals } = useSymbolIntervals(symbol)
  const [emaInput, setEmaInput] = useState("")
  const macd = indicators.macd
  const [indicatorMenuOpen, setIndicatorMenuOpen] = useState(false)
  const indicatorMenuRef = useRef<HTMLDivElement | null>(null)
  const [macdFastInput, setMacdFastInput] = useState(String(macd.fast))
  const [macdSlowInput, setMacdSlowInput] = useState(String(macd.slow))
  const [macdSignalInput, setMacdSignalInput] = useState(String(macd.signal))

  useEffect(() => {
    setMacdFastInput(String(macd.fast))
    setMacdSlowInput(String(macd.slow))
    setMacdSignalInput(String(macd.signal))
  }, [macd.fast, macd.slow, macd.signal])

  useEffect(() => {
    if (!indicatorMenuOpen) return

    const handleClick = (event: MouseEvent) => {
      if (!indicatorMenuRef.current) return
      if (!indicatorMenuRef.current.contains(event.target as Node)) {
        setIndicatorMenuOpen(false)
      }
    }

    document.addEventListener("mousedown", handleClick)
    return () => {
      document.removeEventListener("mousedown", handleClick)
    }
  }, [indicatorMenuOpen])

  const sortedEmaPeriods = useMemo(() => [...emaPeriods].sort((a, b) => a - b), [emaPeriods])

  const handleAddEma = () => {
    const parsed = Number.parseInt(emaInput, 10)
    if (!Number.isFinite(parsed) || parsed <= 0) return
    addEma(parsed)
    setEmaInput("")
  }

  const handleApplyMACDParams = useCallback(() => {
    const fastValue = Number.parseInt(macdFastInput, 10)
    const slowValue = Number.parseInt(macdSlowInput, 10)
    const signalValue = Number.parseInt(macdSignalInput, 10)

    setMACDParams({
      fast: Number.isFinite(fastValue) ? fastValue : macd.fast,
      slow: Number.isFinite(slowValue) ? slowValue : macd.slow,
      signal: Number.isFinite(signalValue) ? signalValue : macd.signal,
    })
  }, [macd.fast, macd.slow, macd.signal, macdFastInput, macdSlowInput, macdSignalInput, setMACDParams])

  const handleMacdInputKeyDown = useCallback(
    (event: React.KeyboardEvent<HTMLInputElement>) => {
      if (event.key === "Enter") {
        event.preventDefault()
        handleApplyMACDParams()
      }
    },
    [handleApplyMACDParams],
  )

  const toggleIndicatorMenu = useCallback(() => {
    setIndicatorMenuOpen((prev) => !prev)
  }, [])

  const isAddDisabled = !emaInput.trim() || Number.parseInt(emaInput, 10) <= 0

  const intervalOptions = useMemo(() => {
    const normalized = normalizeIntervals(symbolIntervals)

    if (normalized.some((item) => item.value === interval)) {
      return normalized
    }

    return [...normalized, { value: interval, label: interval }]
  }, [interval, symbolIntervals])

  return (
    <div className={cn("flex items-center gap-4 p-4 bg-card border-b border-border", className)}>
      {/* Symbol Search */}
      <div className="relative">
        <Search className="absolute left-3 top-1/2 -translate-y-1/2 h-4 w-4 text-muted-foreground" />
        <Input
          placeholder="Search symbol..."
          value={symbol}
          onChange={(e) => setSymbol(e.target.value.toUpperCase())}
          className="pl-9 w-48 bg-input border-border"
        />
      </div>

      <Separator orientation="vertical" className="h-6" />

      {/* Interval Selector */}
      <Select value={interval} onValueChange={(value: Interval) => setInterval(value)}>
        <SelectTrigger className="w-20 bg-input border-border">
          <SelectValue />
        </SelectTrigger>
        <SelectContent>
          {intervalOptions.map((item) => (
            <SelectItem key={item.value} value={item.value}>
              {item.label}
            </SelectItem>
          ))}
        </SelectContent>
      </Select>

      <Separator orientation="vertical" className="h-6" />

      {/* EMA Controls */}
      <div className="flex items-center gap-2">
        <Input
          value={emaInput}
          onChange={(event) => setEmaInput(event.target.value.replace(/[^0-9]/g, ""))}
          onKeyDown={(event) => {
            if (event.key === "Enter") {
              event.preventDefault()
              handleAddEma()
            }
          }}
          placeholder="EMA period"
          inputMode="numeric"
          className="w-24 bg-input border-border"
        />
        <Button variant="secondary" size="sm" onClick={handleAddEma} disabled={isAddDisabled}>
          Add EMA
        </Button>
        <div className="flex items-center gap-2">
          {sortedEmaPeriods.map((period) => (
            <Badge
              key={period}
              variant="outline"
              className="flex items-center gap-1 border"
              style={{ borderColor: getEmaColor(period), color: getEmaColor(period) }}
            >
              EMA {period}
              <button
                type="button"
                className="ml-1 inline-flex h-4 w-4 items-center justify-center rounded-full hover:bg-muted/40"
                onClick={() => removeEma(period)}
                aria-label={`Remove EMA ${period}`}
              >
                <X className="h-3 w-3" />
              </button>
            </Badge>
          ))}
        </div>
      </div>

      <Separator orientation="vertical" className="h-6" />

      <div ref={indicatorMenuRef} className="relative">
        <Button variant="secondary" size="sm" onClick={toggleIndicatorMenu}>
          Indicators
        </Button>
        {indicatorMenuOpen && (
          <div className="absolute left-0 top-full z-20 mt-2 w-64 rounded-md border border-border bg-card p-4 shadow-lg">
            <div className="flex items-center justify-between">
              <span className="text-sm font-medium">MACD</span>
              <Switch checked={macd.visible} onCheckedChange={() => toggleMACD()} />
            </div>
            <div className="mt-3 grid grid-cols-3 gap-3 text-xs text-muted-foreground">
              <label className="flex flex-col gap-1">
                Fast
                <Input
                  value={macdFastInput}
                  onChange={(event) => setMacdFastInput(event.target.value.replace(/[^0-9]/g, ""))}
                  onBlur={handleApplyMACDParams}
                  onKeyDown={handleMacdInputKeyDown}
                  inputMode="numeric"
                  className="bg-input border-border"
                />
              </label>
              <label className="flex flex-col gap-1">
                Slow
                <Input
                  value={macdSlowInput}
                  onChange={(event) => setMacdSlowInput(event.target.value.replace(/[^0-9]/g, ""))}
                  onBlur={handleApplyMACDParams}
                  onKeyDown={handleMacdInputKeyDown}
                  inputMode="numeric"
                  className="bg-input border-border"
                />
              </label>
              <label className="flex flex-col gap-1">
                Signal
                <Input
                  value={macdSignalInput}
                  onChange={(event) => setMacdSignalInput(event.target.value.replace(/[^0-9]/g, ""))}
                  onBlur={handleApplyMACDParams}
                  onKeyDown={handleMacdInputKeyDown}
                  inputMode="numeric"
                  className="bg-input border-border"
                />
              </label>
            </div>
          </div>
        )}
      </div>

      {/* Chart Controls */}
      <div className="flex items-center gap-1">
        <Button variant="ghost" size="sm" className="h-8 w-8 p-0">
          <ZoomIn className="h-4 w-4" />
        </Button>
        <Button variant="ghost" size="sm" className="h-8 w-8 p-0">
          <ZoomOut className="h-4 w-4" />
        </Button>
        <Button variant="ghost" size="sm" className="h-8 w-8 p-0">
          <RotateCcw className="h-4 w-4" />
        </Button>
      </div>

      {/* Status */}
      <div className="flex items-center gap-2 ml-auto">
        <Badge variant="outline" className="text-xs font-mono uppercase tracking-wide">
          Live data
        </Badge>
        <div className="text-sm text-muted-foreground">Streaming real market updates</div>
        {children}
      </div>
    </div>
  )
}
