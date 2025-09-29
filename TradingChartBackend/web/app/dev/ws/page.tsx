"use client";

import { useEffect, useMemo, useRef, useState } from "react";

type MessageEntry = {
  id: number;
  receivedAt: Date;
  payload: string;
};

type ConnectionStatus = "connecting" | "open" | "closed" | "error";

const MAX_MESSAGES = 200;
const MAX_PREVIEW_LENGTH = 500;

const truncate = (value: string) =>
  value.length > MAX_PREVIEW_LENGTH
    ? `${value.slice(0, MAX_PREVIEW_LENGTH)}…`
    : value;

const formatPayload = (raw: MessageEvent["data"]) => {
  if (typeof raw === "string") {
    try {
      const parsed = JSON.parse(raw);
      return truncate(JSON.stringify(parsed, null, 2));
    } catch (error) {
      return truncate(raw);
    }
  }

  if (raw instanceof ArrayBuffer) {
    return `ArrayBuffer(${raw.byteLength} bytes)`;
  }

  if (typeof Blob !== "undefined" && raw instanceof Blob) {
    return `Blob(${raw.size} bytes)`;
  }

  return truncate(String(raw));
};

const statusLabels: Record<ConnectionStatus, string> = {
  connecting: "Connecting",
  open: "Connected",
  closed: "Disconnected",
  error: "Error",
};

export default function WebSocketDevPage() {
  const wsUrl = process.env.NEXT_PUBLIC_WS_API_BASE;
  const [status, setStatus] = useState<ConnectionStatus>("connecting");
  const [messages, setMessages] = useState<MessageEntry[]>([]);
  const [eventCount, setEventCount] = useState(0);
  const reconnectTimeout = useRef<ReturnType<typeof setTimeout> | null>(null);

  useEffect(() => {
    if (!wsUrl) {
      setStatus("error");
      return;
    }

    let socket: WebSocket | null = null;
    let attempts = 0;
    let cancelled = false;

    const connect = () => {
      setStatus("connecting");

      socket = new WebSocket(wsUrl);

      socket.addEventListener("open", () => {
        attempts = 0;
        setStatus("open");
      });

        socket.addEventListener("message", (event: MessageEvent) => {
          setEventCount((count) => count + 1);
          setMessages((prev) => {
            const next: MessageEntry = {
              id: Date.now() + Math.random(),
              receivedAt: new Date(),
              payload: formatPayload(event.data),
            };

            return [next, ...prev].slice(0, MAX_MESSAGES);
          });
        });

      socket.addEventListener("error", () => {
        setStatus("error");
      });

      socket.addEventListener("close", () => {
        setStatus("closed");

        if (cancelled) {
          return;
        }

        const timeout = Math.min(30000, 1000 * 2 ** attempts);
        attempts += 1;

        reconnectTimeout.current = setTimeout(connect, timeout);
      });
    };

    connect();

    return () => {
      cancelled = true;

      if (reconnectTimeout.current) {
        clearTimeout(reconnectTimeout.current);
        reconnectTimeout.current = null;
      }

        if (
          socket &&
          (socket.readyState === WebSocket.OPEN ||
            socket.readyState === WebSocket.CONNECTING)
        ) {
          socket.close();
        }
      };
    }, [wsUrl]);

  const statusLabel = useMemo(() => statusLabels[status] ?? status, [status]);

  return (
    <main className="mx-auto flex min-h-screen w-full max-w-4xl flex-col gap-6 p-6 font-mono text-sm">
      <header className="flex flex-col gap-1">
        <h1 className="text-2xl font-semibold">WebSocket Dev Console</h1>
        <p>
          Endpoint: <code>{wsUrl ?? "(missing NEXT_PUBLIC_WS_API_BASE)"}</code>
        </p>
        <p>
          Status: <span className="font-semibold">{statusLabel}</span>
        </p>
        <p>
          Events received: <span className="font-semibold">{eventCount}</span>
        </p>
      </header>

      {wsUrl ? null : (
        <div className="rounded border border-red-500 bg-red-100/80 p-4 text-red-900">
          Configure <code>NEXT_PUBLIC_WS_API_BASE</code> to enable the WebSocket
          connection.
        </div>
      )}

      <section className="flex-1 overflow-auto rounded border bg-black/90 p-4 text-white">
        {messages.length === 0 ? (
          <p className="text-gray-400">Waiting for messages…</p>
        ) : (
          <ul className="flex flex-col gap-4">
            {messages.map((message) => (
              <li key={message.id} className="border-b border-white/20 pb-4">
                <time className="block text-xs text-gray-400">
                  {message.receivedAt.toLocaleTimeString()}
                </time>
                <pre className="whitespace-pre-wrap break-words">
                  {message.payload}
                </pre>
              </li>
            ))}
          </ul>
        )}
      </section>
    </main>
  );
}
