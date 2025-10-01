import type {
  DeepPartial,
  IChartApi,
  ITimeScaleApi,
  LogicalRange,
  TimeRange,
  TimeScaleOptions,
} from "lightweight-charts"

export type SyncOpts = {
  linkBarSpacing?: boolean
}

export const BASE_TIME_SCALE_OPTIONS: DeepPartial<TimeScaleOptions> = {
  rightOffset: 0,
  fixRightEdge: false,
  lockVisibleTimeRangeOnResize: false,
  shiftVisibleRangeOnNewBar: true,
}

export function syncTimeScales(a: IChartApi, b: IChartApi, opts: SyncOpts = {}) {
  const linkBarSpacing = opts.linkBarSpacing ?? true
  let lock = false

  const ta = a.timeScale()
  const tb = b.timeScale()

  const copyRange = (fromTs: ITimeScaleApi, toTs: ITimeScaleApi) => {
    const r = fromTs.getVisibleLogicalRange()
    if (!r) return
    try {
      toTs.setVisibleLogicalRange({ from: r.from, to: r.to })
    } catch {
      // ignore
    }
  }

  const copySpacing = (fromTs: ITimeScaleApi, toTs: ITimeScaleApi) => {
    if (!linkBarSpacing) return
    const s = fromTs.options().barSpacing
    if (typeof s === "number") {
      try {
        toTs.applyOptions({ barSpacing: s } as DeepPartial<TimeScaleOptions>)
      } catch {
        // ignore
      }
    }
  }

  const handleLogicalFrom = (r: LogicalRange | null) => {
    if (lock || !r) return
    lock = true
    copyRange(ta, tb)
    copySpacing(ta, tb)
    lock = false
  }

  const handleLogicalTo = (r: LogicalRange | null) => {
    if (lock || !r) return
    lock = true
    copyRange(tb, ta)
    copySpacing(tb, ta)
    lock = false
  }

  const handleTimeFrom = (r: TimeRange | null) => {
    if (lock || !r) return
    lock = true
    try {
      tb.setVisibleRange({ from: r.from, to: r.to })
    } catch {
      // ignore
    }
    lock = false
  }

  const handleTimeTo = (r: TimeRange | null) => {
    if (lock || !r) return
    lock = true
    try {
      ta.setVisibleRange({ from: r.from, to: r.to })
    } catch {
      // ignore
    }
    lock = false
  }

  ta.subscribeVisibleLogicalRangeChange(handleLogicalFrom)
  tb.subscribeVisibleLogicalRangeChange(handleLogicalTo)
  ta.subscribeVisibleTimeRangeChange(handleTimeFrom)
  tb.subscribeVisibleTimeRangeChange(handleTimeTo)

  lock = true
  const rangeA = ta.getVisibleLogicalRange()
  const rangeB = tb.getVisibleLogicalRange()
  if (rangeA) {
    copyRange(ta, tb)
  } else if (rangeB) {
    copyRange(tb, ta)
  }
  copySpacing(ta, tb)
  copySpacing(tb, ta)
  lock = false

  return () => {
    ta.unsubscribeVisibleLogicalRangeChange(handleLogicalFrom)
    tb.unsubscribeVisibleLogicalRangeChange(handleLogicalTo)
    ta.unsubscribeVisibleTimeRangeChange(handleTimeFrom)
    tb.unsubscribeVisibleTimeRangeChange(handleTimeTo)
  }
}
