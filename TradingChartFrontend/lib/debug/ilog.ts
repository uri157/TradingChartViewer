let _enabled = false
let _session: string | null = null
const counters = new Map<string, number>()
const lastLogAt = new Map<string, number>()

export function indicatorDebugEnabled() {
  if (typeof window === "undefined") return false
  if (!_enabled) _enabled = process.env.NEXT_PUBLIC_INDICATOR_DEBUG === "1"
  if (_enabled && _session == null) {
    _session = Math.floor(Math.random() * 1e6).toString(36)
  }
  return _enabled
}

export function ilog(tag: string, payload?: unknown, throttleMs = 0) {
  if (!indicatorDebugEnabled()) return
  const now = performance.now()
  if (throttleMs > 0) {
    const last = lastLogAt.get(tag) ?? 0
    if (now - last < throttleMs) return
    lastLogAt.set(tag, now)
  }
  const n = (counters.get(tag) ?? 0) + 1
  counters.set(tag, n)
  // eslint-disable-next-line no-console
  const session = _session ?? "debug"
  console.log(`[IND:${session}] ${tag}#${n} @${now.toFixed(1)}ms`, payload ?? "")
}

export function igroup(tag: string, payload?: unknown) {
  if (!indicatorDebugEnabled()) return
  // eslint-disable-next-line no-console
  console.groupCollapsed(`%c[IND:%s] %s`, "color:#16a34a", _session ?? "debug", tag)
  if (payload) console.log(payload)
}

export function igroupEnd() {
  if (!indicatorDebugEnabled()) return
  // eslint-disable-next-line no-console
  console.groupEnd()
}

export function diffRange(
  a?: { from: number; to: number } | null,
  b?: { from: number; to: number } | null,
) {
  const nf = (x?: number) => (Number.isFinite(x!) ? Number(x).toFixed(3) : "NaN")
  return {
    a: a ? { from: nf(a.from), to: nf(a.to) } : null,
    b: b ? { from: nf(b.from), to: nf(b.to) } : null,
    df: a && b ? (Number(b.from) - Number(a.from)).toFixed(3) : "NaN",
    dt: a && b ? (Number(b.to) - Number(a.to)).toFixed(3) : "NaN",
  }
}
