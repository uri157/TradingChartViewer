#!/usr/bin/env bash
set -euo pipefail

BASE_URL=${BASE_URL:-http://localhost:3000}
ENDPOINTS=(
  "/api/v1/candles"
  "/api/v1/symbols"
  "/api/v1/intervals?symbol=BTCUSDT"
)

for endpoint in "${ENDPOINTS[@]}"; do
  echo "=== GET ${BASE_URL}${endpoint} ==="
  headers_file=$(mktemp)
  trap 'rm -f "$headers_file"' EXIT

  curl_status=0
  body=$(curl -sS -D "$headers_file" "${BASE_URL}${endpoint}" 2>&1) || curl_status=$?

  status_line=$(head -n 1 "$headers_file" 2>/dev/null || echo "")

  if [[ -n "$status_line" ]]; then
    echo "Status: $status_line"
  else
    echo "Status: (no response)"
  fi

  if [[ $curl_status -ne 0 ]]; then
    echo "Curl error (exit code $curl_status):"
    printf '%s\n' "$body"
  else
    if [[ -z "$body" ]]; then
      echo "Body: (empty)"
    else
      printf 'Body (first 200 bytes): '
      printf '%s' "$body" | head -c 200
      echo
    fi
  fi

  echo
  rm -f "$headers_file"
  trap - EXIT

done
