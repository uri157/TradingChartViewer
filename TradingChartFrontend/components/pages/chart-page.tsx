"use client"

import {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
  type MouseEvent as ReactMouseEvent,
} from "react"
import { useSnapshot } from "@/lib/api/queries"
import { useChartStore } from "@/lib/state/use-chart-store"
import { useLiveData } from "@/hooks/use-live-data"
import { CandleChart, type CandleChartControls } from "@/components/chart/candle-chart"
import { IndicatorChart, type IndicatorChartControls } from "@/components/chart/indicator-chart"
import { PriceScale } from "@/components/chart/price-scale"
import { PairBadge } from "@/components/overlays/pair-badge"
import { TopBar } from "@/components/header/top-bar"
import { SymbolList } from "@/components/sidebar/symbol-list"
import { MobileSymbolSheet } from "@/components/layout/mobile-symbol-sheet"
import { LoadingState } from "@/components/feedback/loading-state"
import { ErrorBanner } from "@/components/feedback/error-banner"
import { useMobile } from "@/hooks/use-mobile"
import { Button } from "@/components/ui/button"
import { X } from "lucide-react"
import { clamp } from "@/lib/utils"
import { ilog, igroup, igroupEnd } from "@/lib/debug/ilog"
import type { CandleDTO } from "@/lib/api/types"

const DEBUG_ENABLED = process.env.NEXT_PUBLIC_CHART_DEBUG === "1"

const logDebug = (...args: unknown[]) => {
  if (DEBUG_ENABLED && typeof window !== "undefined") {
    // eslint-disable-next-line no-console
    console.debug(...args)
  }
}

function findCandleIndexByTime(candles: CandleDTO[], time: number): number {
  for (let index = candles.length - 1; index >= 0; index--) {
    if (candles[index]?.t === time) {
      return index
    }
  }
  return -1
}

function candlesAreEqual(a: CandleDTO | undefined, b: CandleDTO): boolean {
  if (!a) return false
  return a.t === b.t && a.o === b.o && a.h === b.h && a.l === b.l && a.c === b.c && a.v === b.v
}

function upsertCandle(candles: CandleDTO[], candle: CandleDTO): CandleDTO[] {
  const index = findCandleIndexByTime(candles, candle.t)
  if (index !== -1) {
    if (candlesAreEqual(candles[index], candle)) {
      return candles
    }
    const next = candles.slice()
    next[index] = candle
    return next
  }

  if (!candles.length) {
    return [candle]
  }

  const next = candles.slice()
  let insertIndex = candles.length
  for (let idx = candles.length - 1; idx >= 0; idx--) {
    const current = candles[idx]
    if (!current) continue
    if (current.t <= candle.t) {
      insertIndex = current.t === candle.t ? idx : idx + 1
      break
    }
    insertIndex = idx
  }

  next.splice(insertIndex, 0, candle)
  return next
}

export function ChartPage() {
  const symbol = useChartStore((state) => state.symbol)
  const interval = useChartStore((state) => state.interval)
  const macd = useChartStore((state) => state.indicators.macd)
  const hideMACD = useChartStore((state) => state.hideMACD)
  const setMACDHeight = useChartStore((state) => state.setMACDHeight)
  const [currentPrice, setCurrentPrice] = useState<number | null>(null)
  const [priceControls, setPriceControls] = useState<CandleChartControls | null>(null)
  const [indicatorControls, setIndicatorControls] = useState<IndicatorChartControls | null>(null)
  const [isResizingIndicator, setIsResizingIndicator] = useState(false)
  const [mergedCandles, setMergedCandles] = useState<CandleDTO[]>([])
  const [hydrated, setHydrated] = useState(false)
  const mergedContextRef = useRef<{ symbol: string; interval: string }>({ symbol, interval })
  const resizeStartYRef = useRef(0)
  const resizeStartHeightRef = useRef(0)
  const resizeRafRef = useRef<number | null>(null)
  const isCrosshairActiveRef = useRef(false)
  const crosshairSyncingRef = useRef(false)
  const isMobile = useMobile()

  const { data: snapshot, isLoading, error, refetch } = useSnapshot(symbol, interval, 600)

  const baseCandles = useMemo(() => {
    if (!snapshot?.candles?.length) {
      return []
    }
    if (snapshot.symbol !== symbol || snapshot.interval !== interval) {
      return []
    }
    return snapshot.candles
  }, [snapshot, symbol, interval])

  const indicatorCandles = useMemo(() => {
    const contextMatches =
      mergedContextRef.current.symbol === symbol &&
      mergedContextRef.current.interval === interval

    if (contextMatches && mergedCandles.length) {
      return mergedCandles
    }

    return baseCandles
  }, [baseCandles, mergedCandles, symbol, interval])

  const lastClose =
    indicatorCandles.length && indicatorCandles[indicatorCandles.length - 1]
      ? Number(indicatorCandles[indicatorCandles.length - 1]?.c)
      : null

  const handleLiveAppend = useCallback(
    (candle: CandleDTO) => {
      priceControls?.appendCandle(candle)
      logDebug("[LIVE] append handler", candle.t)
      const previousContext = mergedContextRef.current
      const nextContext = { symbol, interval }
      mergedContextRef.current = nextContext
      setMergedCandles((prev) => {
        const contextMatches =
          previousContext.symbol === nextContext.symbol &&
          previousContext.interval === nextContext.interval
        const source = contextMatches && prev.length ? prev : baseCandles
        return upsertCandle(source, candle)
      })
    },
    [baseCandles, interval, priceControls, symbol],
  )

  const handleLiveReplace = useCallback(
    (candle: CandleDTO) => {
      priceControls?.replaceLastCandle(candle)
      logDebug("[LIVE] replace handler", candle.t)
      const previousContext = mergedContextRef.current
      const nextContext = { symbol, interval }
      mergedContextRef.current = nextContext
      setMergedCandles((prev) => {
        const contextMatches =
          previousContext.symbol === nextContext.symbol &&
          previousContext.interval === nextContext.interval
        const source = contextMatches && prev.length ? prev : baseCandles
        return upsertCandle(source, candle)
      })
    },
    [baseCandles, interval, priceControls, symbol],
  )

  const liveHandlers = useMemo(
    () => ({
      onAppend: handleLiveAppend,
      onReplace: handleLiveReplace,
      getLastTime: () => priceControls?.getLastTime?.() ?? null,
    }),
    [handleLiveAppend, handleLiveReplace, priceControls],
  )

  useLiveData(symbol, interval, liveHandlers)

  useEffect(() => {
    mergedContextRef.current = { symbol, interval }
    setMergedCandles([])
  }, [interval, symbol])

  useEffect(() => {
    ilog("page:mount", { symbol, interval })
  }, [])

  useEffect(() => {
    setHydrated(true)
  }, [])

  useEffect(() => {
    ilog("page:macdVisibility", { visible: macd.visible })
  }, [macd.visible])

  useEffect(() => {
    ilog("page:macdHeight", { h: macd.heightPx }, 50)
  }, [macd.heightPx])

  // useEffect(() => {
  //   if (!isDebug()) return
  //   if (snapshot?.candles?.length) {
  //     console.log("[page] snapshot ready", snapshot.candles.length) // Debug: confirm page received snapshot data.
  //   }
  // }, [snapshot])

  // useEffect(() => {
  //   if (!isDebug()) return
  //   if (error) {
  //     const message = error instanceof Error ? error.message : String(error)
  //     console.error(`[page] snapshot error ${message}`, error) // Debug: expose snapshot load failures.
  //   }
  // }, [error])

  useEffect(() => {
    if (!isCrosshairActiveRef.current) {
      if (typeof lastClose === "number" && Number.isFinite(lastClose)) {
        setCurrentPrice(lastClose)
      } else {
        setCurrentPrice(null)
      }
    }
  }, [lastClose])

  const handlePriceCrosshair = useCallback(
    (payload: { price: number | null; time: number | null }) => {
      ilog("page:xhair:price", payload, 16)
      if (payload.price != null && Number.isFinite(payload.price)) {
        isCrosshairActiveRef.current = true
        setCurrentPrice(payload.price)
      } else {
        isCrosshairActiveRef.current = false
        if (typeof lastClose === "number" && Number.isFinite(lastClose)) {
          setCurrentPrice(lastClose)
        } else {
          setCurrentPrice(null)
        }
      }

      if (crosshairSyncingRef.current) return
      if (indicatorControls) {
        crosshairSyncingRef.current = true
        const nextTime =
          typeof payload.time === "number" && Number.isFinite(payload.time) ? payload.time : null
        indicatorControls.setExternalCrosshair(nextTime)
        requestAnimationFrame(() => {
          crosshairSyncingRef.current = false
        })
      }
    },
    [indicatorControls, lastClose],
  )

  const handleIndicatorCrosshair = useCallback(
    ({ time }: { time: number | null }) => {
      ilog("page:xhair:indic", { time }, 16)
      if (crosshairSyncingRef.current) return
      if (priceControls) {
        crosshairSyncingRef.current = true
        const nextTime = time ?? null
        priceControls.moveCrosshair(nextTime)
        requestAnimationFrame(() => {
          crosshairSyncingRef.current = false
        })
      }
    },
    [priceControls],
  )

  useEffect(() => {
    const payload = { hasIndicator: !!indicatorControls, hasPrice: !!priceControls }
    igroup("page:sync", payload)
    ilog("page:sync:start", payload)
    if (!indicatorControls) {
      igroupEnd()
      return
    }
    indicatorControls.syncWith(priceControls?.chart ?? null)
    ilog("page:sync:done")
    igroupEnd()
  }, [indicatorControls, priceControls])

  const handleResizeStart = useCallback(
    (event: ReactMouseEvent<HTMLDivElement>) => {
      event.preventDefault()
      resizeStartYRef.current = event.clientY
      resizeStartHeightRef.current = macd.heightPx
      setIsResizingIndicator(true)
    },
    [macd.heightPx],
  )

  useEffect(() => {
    if (!isResizingIndicator) return

    const handleMouseMove = (event: MouseEvent) => {
      if (resizeRafRef.current != null) {
        return
      }

      const clientY = event.clientY
      resizeRafRef.current = requestAnimationFrame(() => {
        resizeRafRef.current = null
        const delta = clientY - resizeStartYRef.current
        const windowHeight = typeof window !== "undefined" ? window.innerHeight : resizeStartHeightRef.current
        const maxHeight = Math.max(120, Math.floor(windowHeight * 0.6))
        const next = clamp(resizeStartHeightRef.current + delta, 120, maxHeight)
        ilog("page:resize:move", { next }, 33)
        setMACDHeight(next)
      })
    }

    const handleMouseUp = () => {
      setIsResizingIndicator(false)
    }

    window.addEventListener("mousemove", handleMouseMove)
    window.addEventListener("mouseup", handleMouseUp)

    return () => {
      window.removeEventListener("mousemove", handleMouseMove)
      window.removeEventListener("mouseup", handleMouseUp)
      if (resizeRafRef.current != null) {
        cancelAnimationFrame(resizeRafRef.current)
        resizeRafRef.current = null
      }
    }
  }, [isResizingIndicator, setMACDHeight])

  useEffect(() => {
    if (!macd.visible) {
      setIsResizingIndicator(false)
    }
  }, [macd.visible])

  const fallbackPrice =
    typeof lastClose === "number" && Number.isFinite(lastClose) ? lastClose : null

  if (!hydrated) {
    return <LoadingState message="Loading chart..." className="h-screen" />
  }

  if (error) {
    return (
      <div className="h-screen flex items-center justify-center p-4">
        <ErrorBanner error="Failed to load chart data" onRetry={() => refetch()} className="max-w-md" />
      </div>
    )
  }

  const hasCandles = (snapshot?.candles?.length ?? 0) > 0

  return (
    <div className="h-screen flex flex-col bg-background">
      {/* Top Bar */}
      <TopBar className="flex-shrink-0">
        {isMobile && (
          <div className="ml-auto">
            <MobileSymbolSheet />
          </div>
        )}
      </TopBar>

      {/* Main Content */}
      <div className="flex-1 flex overflow-hidden">
        <div className="flex-1 flex">
          <div className="flex-1 flex flex-col">
            <div className="flex flex-1">
              <div className="flex-1 flex flex-col relative overflow-hidden bg-card/0">
                <div className="flex-1 relative">
                  {isLoading ? (
                    <LoadingState message="Loading chart..." />
                  ) : hasCandles ? (
                    <>
                      <CandleChart
                        data={snapshot?.candles ?? []}
                        interval={interval}
                        className="h-full"
                        onCrosshairMove={handlePriceCrosshair}
                        onReady={setPriceControls}
                      />
                      <PairBadge symbol={symbol} interval={interval} />
                    </>
                  ) : (
                    <div className="h-full flex items-center justify-center text-muted-foreground">
                      No chart data available
                    </div>
                  )}
                </div>
                {macd.visible && (
                  <>
                    <div
                      className="h-1 cursor-row-resize bg-border/40 hover:bg-border transition-colors"
                      onMouseDown={handleResizeStart}
                    />
                    <div
                      className="relative bg-card overflow-hidden min-h-[120px]"
                      style={{ height: macd.heightPx }}
                    >
                      <IndicatorChart
                        candles={indicatorCandles}
                        interval={interval}
                        params={{ fast: macd.fast, slow: macd.slow, signal: macd.signal }}
                        onCrosshairMove={handleIndicatorCrosshair}
                        onReady={setIndicatorControls}
                        className="absolute inset-0"
                      />
                      <Button
                        variant="ghost"
                        size="icon"
                        onClick={hideMACD}
                        className="absolute right-2 top-2 z-10 h-8 w-8 rounded-full bg-background/80 hover:bg-background"
                        aria-label="Hide MACD"
                      >
                        <X className="h-4 w-4" />
                      </Button>
                    </div>
                  </>
                )}
              </div>
              <PriceScale
                currentPrice={currentPrice ?? fallbackPrice}
                symbol={symbol}
                className="w-20"
              />
            </div>
          </div>
        </div>
        {!isMobile && <SymbolList className="w-80" />}
      </div>
    </div>
  )
}
