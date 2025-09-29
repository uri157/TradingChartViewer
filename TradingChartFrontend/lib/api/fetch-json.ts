import { resolveApiUrl } from "../apiClient"

export class ApiError extends Error {
  status: number
  body: unknown

  constructor(message: string, status: number, body: unknown) {
    super(message)
    this.name = "ApiError"
    this.status = status
    this.body = body
  }
}

function safeParseJSON(text: string): unknown {
  if (!text) {
    return null
  }

  try {
    return JSON.parse(text)
  } catch {
    return text
  }
}

export async function fetchJSON<T>(path: string, init?: RequestInit): Promise<T> {
  const target = resolveApiUrl(path)
  const response = await fetch(target, {
    ...init,
    headers: {
      Accept: "application/json",
      ...(init?.headers ?? {}),
    },
  })

  const text = await response.text()
  const body = safeParseJSON(text)

  if (!response.ok) {
    throw new ApiError(
      `Request failed with status ${response.status}`,
      response.status,
      body,
    )
  }

  return body as T
}
