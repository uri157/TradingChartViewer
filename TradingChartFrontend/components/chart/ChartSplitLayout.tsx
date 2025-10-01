"use client"

import {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
  type ReactNode,
} from "react"

import type { MacdChartHandle } from "@/components/chart/MacdChart"
import type { PriceChartHandle } from "@/components/chart/PriceChart"
import {
  DEFAULT_PRICE_TO_MACD_RATIO,
  usePriceMacdLayout,
} from "@/lib/layout/use-price-macd-layout"
import { clamp } from "@/lib/utils"

const MIN_PRICE_HEIGHT = 160
const MIN_MACD_HEIGHT = 96
const SPLITTER_HEIGHT = 10

export interface ChartSplitLayoutProps {
  className?: string
  pricePane: ReactNode
  macdPane: ReactNode
  priceChartRef: React.MutableRefObject<PriceChartHandle | null>
  macdChartRef: React.MutableRefObject<MacdChartHandle | null>
}

interface PointerState {
  active: boolean
  pointerId: number | null
  pointerType: string
  startClientY: number
  startPriceHeight: number
}

const clampSize = (value: number): number => {
  if (!Number.isFinite(value)) return 0
  return Math.max(0, Math.round(value))
}

const resolveBounds = (total: number) => {
  const safeTotal = clampSize(total)
  if (safeTotal <= 0) {
    return { minPrice: 0, maxPrice: 0 }
  }
  const minPrice = Math.min(MIN_PRICE_HEIGHT, Math.max(0, safeTotal - MIN_MACD_HEIGHT))
  const maxPrice = Math.max(safeTotal - MIN_MACD_HEIGHT, minPrice)
  return { minPrice, maxPrice }
}

const computeHeights = (total: number, ratio: number) => {
  const safeTotal = clampSize(total)
  if (safeTotal <= 0) {
    return { price: 0, macd: 0 }
  }
  const { minPrice, maxPrice } = resolveBounds(safeTotal)
  const desiredPrice = clampSize(safeTotal * ratio)
  const price = clamp(desiredPrice, minPrice, maxPrice)
  const macd = clampSize(safeTotal - price)
  return { price, macd }
}

export function ChartSplitLayout({
  className,
  pricePane,
  macdPane,
  priceChartRef,
  macdChartRef,
}: ChartSplitLayoutProps) {
  const containerRef = useRef<HTMLDivElement | null>(null)
  const pricePaneRef = useRef<HTMLDivElement | null>(null)
  const macdPaneRef = useRef<HTMLDivElement | null>(null)
  const splitterRef = useRef<HTMLDivElement | null>(null)
  const pointerStateRef = useRef<PointerState>({
    active: false,
    pointerId: null,
    pointerType: "mouse",
    startClientY: 0,
    startPriceHeight: 0,
  })
  const rafRef = useRef<number | null>(null)
  const lastPointerYRef = useRef(0)
  const [containerHeight, setContainerHeight] = useState(0)
  const { ratio, setRatio, loadInitial } = usePriceMacdLayout()

  useEffect(() => {
    loadInitial()
  }, [loadInitial])

  useEffect(() => {
    const container = containerRef.current
    if (!container) return

    const observer = new ResizeObserver((entries) => {
      const entry = entries[0]
      if (!entry) return
      const nextHeight = clampSize(entry.contentRect.height)
      setContainerHeight(nextHeight)
    })

    observer.observe(container)

    return () => observer.disconnect()
  }, [])

  const bounds = useMemo(() => resolveBounds(containerHeight), [containerHeight])
  const { price: priceHeight, macd: macdHeight } = useMemo(() => {
    return computeHeights(containerHeight, ratio)
  }, [containerHeight, ratio])
  const ariaValueMin = useMemo(() => {
    if (containerHeight <= 0) return 0
    return Math.round((bounds.minPrice / containerHeight) * 100)
  }, [bounds.minPrice, containerHeight])
  const ariaValueMax = useMemo(() => {
    if (containerHeight <= 0) return 100
    return Math.round((bounds.maxPrice / containerHeight) * 100)
  }, [bounds.maxPrice, containerHeight])

  useEffect(() => {
    const pricePaneEl = pricePaneRef.current
    const macdPaneEl = macdPaneRef.current
    const splitterEl = splitterRef.current
    if (pricePaneEl) {
      pricePaneEl.style.height = `${priceHeight}px`
    }
    if (macdPaneEl) {
      macdPaneEl.style.top = `${priceHeight}px`
      macdPaneEl.style.height = `${macdHeight}px`
    }
    if (splitterEl) {
      splitterEl.style.top = `${priceHeight}px`
    }
    if (priceChartRef.current) {
      priceChartRef.current.applySize(priceHeight)
    }
    if (macdChartRef.current) {
      macdChartRef.current.applySize(macdHeight)
    }
  }, [macdChartRef, macdHeight, priceChartRef, priceHeight])

  useEffect(() => {
    if (!priceChartRef.current || !macdChartRef.current) {
      return
    }
    const frame = requestAnimationFrame(() => {
      const priceWidth = priceChartRef.current?.measureRightAxisWidth() ?? 0
      const macdWidth = macdChartRef.current?.measureRightAxisWidth() ?? 0
      const target = Math.max(priceWidth, macdWidth)
      if (target > 0) {
        priceChartRef.current?.setRightAxisWidth(target)
        macdChartRef.current?.setRightAxisWidth(target)
      }
    })
    return () => cancelAnimationFrame(frame)
  }, [macdChartRef, macdHeight, priceChartRef, priceHeight])

  useEffect(() => {
    return () => {
      if (rafRef.current != null) {
        cancelAnimationFrame(rafRef.current)
      }
      document.body.style.cursor = ""
    }
  }, [])

  const applyRatioFromHeight = useCallback(
    (nextPriceHeight: number, total: number) => {
      if (total <= 0) return
      const { minPrice, maxPrice } = resolveBounds(total)
      const constrained = clamp(clampSize(nextPriceHeight), minPrice, maxPrice)
      const nextRatio = total > 0 ? constrained / total : ratio
      setRatio(nextRatio)
    },
    [ratio, setRatio],
  )

  const handlePointerDown = useCallback(
    (event: React.PointerEvent<HTMLDivElement>) => {
      event.preventDefault()
      const total = containerHeight
      if (total <= 0) return
      const splitter = splitterRef.current
      if (!splitter) return
      pointerStateRef.current = {
        active: true,
        pointerId: event.pointerId,
        pointerType: event.pointerType,
        startClientY: event.clientY,
        startPriceHeight: priceHeight,
      }
      lastPointerYRef.current = event.clientY
      splitter.setPointerCapture(event.pointerId)
      document.body.style.cursor = "row-resize"
    },
    [containerHeight, priceHeight],
  )

  const handlePointerMove = useCallback((event: React.PointerEvent<HTMLDivElement>) => {
    const state = pointerStateRef.current
    if (!state.active || state.pointerId !== event.pointerId) {
      return
    }
    if (state.pointerType === "touch") {
      event.preventDefault()
    }
    lastPointerYRef.current = event.clientY
    if (rafRef.current != null) {
      return
    }
    rafRef.current = requestAnimationFrame(() => {
      rafRef.current = null
      const total = containerHeight
      if (total <= 0) return
      const delta = lastPointerYRef.current - state.startClientY
      const nextHeight = state.startPriceHeight + delta
      applyRatioFromHeight(nextHeight, total)
    })
  }, [applyRatioFromHeight, containerHeight])

  const releasePointer = useCallback(() => {
    const state = pointerStateRef.current
    if (!state.active) return
    const splitter = splitterRef.current
    if (splitter && state.pointerId !== null) {
      try {
        splitter.releasePointerCapture(state.pointerId)
      } catch {
        // ignore errors where the pointer is already released
      }
    }
    pointerStateRef.current = {
      active: false,
      pointerId: null,
      pointerType: "mouse",
      startClientY: 0,
      startPriceHeight: 0,
    }
    document.body.style.cursor = ""
  }, [])

  const handlePointerUp = useCallback(() => {
    releasePointer()
  }, [releasePointer])

  const handleDoubleClick = useCallback(() => {
    setRatio(DEFAULT_PRICE_TO_MACD_RATIO)
  }, [setRatio])

  const handleKeyDown = useCallback(
    (event: React.KeyboardEvent<HTMLDivElement>) => {
      if (event.key !== "ArrowUp" && event.key !== "ArrowDown") {
        return
      }
      event.preventDefault()
      const total = containerHeight
      if (total <= 0) return
      const step = event.shiftKey ? 8 : 1
      const direction = event.key === "ArrowUp" ? -1 : 1
      const nextHeight = priceHeight + direction * step
      applyRatioFromHeight(nextHeight, total)
    },
    [applyRatioFromHeight, containerHeight, priceHeight],
  )

  return (
    <div
      ref={containerRef}
      className={className}
      style={{ position: "relative", width: "100%", height: "100%" }}
    >
      <div
        ref={pricePaneRef}
        id="pricePane"
        className="absolute left-0 right-0 overflow-hidden"
        style={{ top: 0, height: `${priceHeight}px` }}
      >
        {pricePane}
      </div>
      <div
        ref={splitterRef}
        id="splitter"
        role="separator"
        aria-orientation="horizontal"
        aria-valuenow={Math.round(ratio * 100)}
        aria-valuemin={ariaValueMin}
        aria-valuemax={ariaValueMax}
        tabIndex={0}
        className="absolute left-0 right-0 z-20 flex items-center justify-center"
        style={{
          height: SPLITTER_HEIGHT,
          top: `${priceHeight}px`,
          transform: `translateY(-${SPLITTER_HEIGHT / 2}px)`,
          cursor: "row-resize",
          touchAction: "none",
        }}
        onPointerDown={handlePointerDown}
        onPointerMove={handlePointerMove}
        onPointerUp={handlePointerUp}
        onPointerCancel={handlePointerUp}
        onLostPointerCapture={handlePointerUp}
        onDoubleClick={handleDoubleClick}
        onKeyDown={handleKeyDown}
      >
        <div className="h-full w-2 rounded-full bg-border" />
      </div>
      <div
        ref={macdPaneRef}
        id="macdPane"
        className="absolute left-0 right-0 overflow-hidden"
        style={{ top: `${priceHeight}px`, height: `${macdHeight}px` }}
      >
        {macdPane}
      </div>
    </div>
  )
}
