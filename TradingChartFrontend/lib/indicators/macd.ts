"use client"

const dlog = (...args: any[]) => {
  if (typeof window === "undefined") return undefined
  if (process.env.NEXT_PUBLIC_INDICATOR_DEBUG !== "1") return undefined
  // eslint-disable-next-line no-console
  return console.debug(...args)
}

export interface MACDParams {
  fast?: number
  slow?: number
  signal?: number
}

export interface NormalizedMACDParams {
  fast: number
  slow: number
  signal: number
}

export interface MACDPoint {
  time: number
  value: number
}

export interface MACDState {
  params: NormalizedMACDParams
  fastCount: number
  slowCount: number
  signalCount: number
  fastSum: number
  slowSum: number
  signalSum: number
  emaFast: number | null
  emaSlow: number | null
  emaSignal: number | null
  lastMacd: number | null
  lastSignal: number | null
  lastHist: number | null
  lastTime: number | null
}

export interface MACDComputationResult {
  macd: MACDPoint[]
  signal: MACDPoint[]
  hist: MACDPoint[]
  state: MACDState
  stateBeforeLast: MACDState | null
}

export interface MACDNextResult {
  macd: number | null
  signal: number | null
  hist: number | null
  state: MACDState
}

function normalizeParams(params?: MACDParams): NormalizedMACDParams {
  const fast = Math.max(1, Math.trunc(params?.fast ?? 12))
  const slow = Math.max(fast + 1, Math.trunc(params?.slow ?? 26))
  const signal = Math.max(1, Math.trunc(params?.signal ?? 9))
  return { fast, slow, signal }
}

function createInitialState(params?: MACDParams): MACDState {
  const normalized = normalizeParams(params)
  return {
    params: normalized,
    fastCount: 0,
    slowCount: 0,
    signalCount: 0,
    fastSum: 0,
    slowSum: 0,
    signalSum: 0,
    emaFast: null,
    emaSlow: null,
    emaSignal: null,
    lastMacd: null,
    lastSignal: null,
    lastHist: null,
    lastTime: null,
  }
}

function cloneState(state: MACDState): MACDState {
  return {
    params: { ...state.params },
    fastCount: state.fastCount,
    slowCount: state.slowCount,
    signalCount: state.signalCount,
    fastSum: state.fastSum,
    slowSum: state.slowSum,
    signalSum: state.signalSum,
    emaFast: state.emaFast,
    emaSlow: state.emaSlow,
    emaSignal: state.emaSignal,
    lastMacd: state.lastMacd,
    lastSignal: state.lastSignal,
    lastHist: state.lastHist,
    lastTime: state.lastTime,
  }
}

function isSameParams(a: NormalizedMACDParams, b: NormalizedMACDParams): boolean {
  return a.fast === b.fast && a.slow === b.slow && a.signal === b.signal
}

export function nextMACD(
  previousState: MACDState | null | undefined,
  close: number,
  params?: MACDParams,
): MACDNextResult {
  const normalized = normalizeParams(params)
  const baseState = previousState && isSameParams(previousState.params, normalized)
    ? cloneState(previousState)
    : createInitialState(normalized)

  const state = baseState
  state.params = normalized

  const price = Number(close)
  if (!Number.isFinite(price)) {
    return {
      macd: null,
      signal: null,
      hist: null,
      state,
    }
  }

  state.fastCount += 1
  state.fastSum += price
  if (state.fastCount === normalized.fast) {
    state.emaFast = state.fastSum / normalized.fast
  } else if (state.fastCount > normalized.fast && state.emaFast != null) {
    const alphaFast = 2 / (normalized.fast + 1)
    state.emaFast = alphaFast * price + (1 - alphaFast) * state.emaFast
  }

  if (state.fastCount % 500 === 0) {
    dlog("[macd:next]", {
      idx: state.fastCount,
      emaFast: state.emaFast,
      emaSlow: state.emaSlow,
      emaSignal: state.emaSignal,
    })
  }

  state.slowCount += 1
  state.slowSum += price
  if (state.slowCount === normalized.slow) {
    state.emaSlow = state.slowSum / normalized.slow
  } else if (state.slowCount > normalized.slow && state.emaSlow != null) {
    const alphaSlow = 2 / (normalized.slow + 1)
    state.emaSlow = alphaSlow * price + (1 - alphaSlow) * state.emaSlow
  }

  let macdValue: number | null = null
  if (state.emaFast != null && state.emaSlow != null) {
    macdValue = state.emaFast - state.emaSlow
    state.lastMacd = macdValue
  } else {
    state.lastMacd = null
  }

  let signalValue: number | null = null
  let histValue: number | null = null

  if (macdValue != null) {
    state.signalCount += 1
    state.signalSum += macdValue

    if (state.signalCount === normalized.signal) {
      state.emaSignal = state.signalSum / normalized.signal
    } else if (state.signalCount > normalized.signal && state.emaSignal != null) {
      const alphaSignal = 2 / (normalized.signal + 1)
      state.emaSignal = alphaSignal * macdValue + (1 - alphaSignal) * state.emaSignal
    }

    if (state.signalCount >= normalized.signal && state.emaSignal != null) {
      signalValue = state.emaSignal
      histValue = macdValue - state.emaSignal
    }
  }

  state.lastSignal = signalValue
  state.lastHist = histValue

  return {
    macd: macdValue,
    signal: signalValue,
    hist: histValue,
    state,
  }
}

export function computeMACD(
  closes: number[],
  times: number[],
  fast = 12,
  slow = 26,
  signal = 9,
): MACDComputationResult {
  const params = { fast, slow, signal }
  let state = createInitialState(params)
  let stateBeforeLast: MACDState | null = null

  const macd: MACDPoint[] = []
  const signalPoints: MACDPoint[] = []
  const hist: MACDPoint[] = []

  dlog("[macd:compute] start", { n: closes.length, fast, slow, signal })

  for (let index = 0; index < closes.length && index < times.length; index++) {
    const close = closes[index]
    const time = times[index]

    if (!Number.isFinite(close) || !Number.isFinite(time)) {
      continue
    }

    const result = nextMACD(state, close, params)
    stateBeforeLast = state
    state = result.state
    state.lastTime = time

    if (result.macd != null) {
      macd.push({ time, value: result.macd })
    }

    if (result.signal != null) {
      signalPoints.push({ time, value: result.signal })
    }

    if (result.hist != null) {
      hist.push({ time, value: result.hist })
    }
  }

  dlog("[macd:compute] end", {
    out: { macd: macd.length, signal: signalPoints.length, hist: hist.length },
    t0: times[0],
    tN: times.at(-1),
  })

  return {
    macd,
    signal: signalPoints,
    hist,
    state,
    stateBeforeLast,
  }
}

export function cloneMACDState(state: MACDState | null): MACDState | null {
  return state ? cloneState(state) : null
}
