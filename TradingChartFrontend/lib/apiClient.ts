const DEV_FALLBACK_API_BASE = "http://localhost:8080";

function normalizeBaseUrl(rawBase: string): string {
  const trimmed = rawBase.trim();
  const withoutTrailingSlash = trimmed.replace(/\/+$/, "");

  if (withoutTrailingSlash.length === 0) {
    throw new Error("API base URL cannot be empty. Set NEXT_PUBLIC_API_URL.");
  }

  return withoutTrailingSlash;
}

function normalizeRelativePath(path: string): string {
  if (path.length === 0) {
    return "";
  }

  const trimmed = path.trim();

  if (trimmed.length === 0) {
    return "";
  }

  return trimmed.replace(/^\/+/, "");
}

function resolveBaseUrl(): string {
  const envValue = process.env.NEXT_PUBLIC_API_URL ?? "";

  if (envValue.trim().length > 0) {
    return normalizeBaseUrl(envValue);
  }

  if (process.env.NODE_ENV === "development") {
    return normalizeBaseUrl(DEV_FALLBACK_API_BASE);
  }

  throw new Error("NEXT_PUBLIC_API_URL must be defined in this environment.");
}

const API_BASE = resolveBaseUrl();

function isAbsoluteUrl(url: string): boolean {
  return /^https?:\/\//i.test(url);
}

export function getApiDisplayBase(): string {
  return API_BASE;
}

export function resolveApiUrl(path: string): string {
  if (isAbsoluteUrl(path)) {
    return path;
  }

  const normalizedPath = normalizeRelativePath(path);

  if (normalizedPath.length === 0) {
    return API_BASE;
  }

  return `${API_BASE}/${normalizedPath}`;
}
