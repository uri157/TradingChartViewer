import { getApiDisplayBase } from "./apiClient"

const DEFAULT_PATH = "/ws"

function normalizeBaseUrl(rawBase: string): string {
  const trimmed = rawBase.trim()
  return trimmed.replace(/\/+$/, "")
}

function normalizePath(path: string): string {
  const trimmed = path.trim()

  if (trimmed.length === 0) {
    return ""
  }

  return trimmed.startsWith("/") ? trimmed : `/${trimmed}`
}

export function resolveWsUrl(path = DEFAULT_PATH): string {
  const hasCustomPath = path !== DEFAULT_PATH
  const envValue = process.env.NEXT_PUBLIC_WS_URL ?? ""
  const normalizedPath = normalizePath(path)

  if (envValue.trim().length > 0) {
    const base = normalizeBaseUrl(envValue)

    if (!hasCustomPath) {
      return base
    }

    return `${base}${normalizedPath}`
  }

  const apiBase = getApiDisplayBase()
  const apiUrl = new URL(apiBase)

  let protocol: string
  if (apiUrl.protocol === "https:") {
    protocol = "wss:"
  } else if (apiUrl.protocol === "http:") {
    protocol = "ws:"
  } else {
    throw new Error(`Unsupported API protocol for WebSocket: ${apiUrl.protocol}`)
  }

  const origin = `${protocol}//${apiUrl.host}`

  if (normalizedPath.length === 0) {
    return origin
  }

  return `${origin}${normalizedPath}`
}
