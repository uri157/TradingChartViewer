export const API_BASE = (process.env.NEXT_PUBLIC_API_BASE ?? "/api").replace(/\/+$/, "")

export const HTTP_API_BASE = (
  process.env.HTTP_API_BASE ?? process.env.NEXT_PUBLIC_HTTP_API_BASE ?? "http://localhost:8080"
).replace(/\/+$/, "")

export const WS_API_BASE = (
  process.env.NEXT_PUBLIC_WS_URL ?? process.env.NEXT_PUBLIC_WS_API_BASE ?? ""
).replace(/\/+$/, "")

export const DEFAULT_LIMIT: number = Number(
  process.env.NEXT_PUBLIC_DEFAULT_LIMIT ?? 600,
)
