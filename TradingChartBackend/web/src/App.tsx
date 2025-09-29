import type { FC } from 'react';
import { useEffect, useMemo, useRef, useState } from 'react';
import {
  CandlestickData,
  IChartApi,
  ISeriesApi,
  UTCTimestamp,
  createChart,
} from 'lightweight-charts';

import './App.css';

type CandlestickSeries = ISeriesApi<'Candlestick'>;

const API_PATH = '/snapshot?symbol=BTCUSDT&interval=1m&limit=200';

const getNumeric = (value: unknown): number | undefined => {
  if (typeof value === 'number' && Number.isFinite(value)) {
    return value;
  }

  if (typeof value === 'string') {
    const numeric = Number.parseFloat(value);
    if (Number.isFinite(numeric)) {
      return numeric;
    }
  }

  return undefined;
};

const getUnixTimeSeconds = (record: Record<string, unknown>): UTCTimestamp | undefined => {
  const candidate =
    getNumeric(record.openTime) ??
    getNumeric(record.time) ??
    getNumeric(record.timestamp) ??
    getNumeric(record.t);

  if (candidate === undefined) {
    return undefined;
  }

  const seconds = candidate > 1e12 ? Math.floor(candidate / 1000) : Math.floor(candidate);
  return seconds as UTCTimestamp;
};

const toCandlestickData = (item: unknown): CandlestickData | null => {
  if (!item || typeof item !== 'object') {
    return null;
  }

  const record = item as Record<string, unknown>;
  const time = getUnixTimeSeconds(record);
  const open = getNumeric(record.open ?? record.o);
  const high = getNumeric(record.high ?? record.h);
  const low = getNumeric(record.low ?? record.l);
  const close = getNumeric(record.close ?? record.c);

  if (
    time === undefined ||
    open === undefined ||
    high === undefined ||
    low === undefined ||
    close === undefined
  ) {
    return null;
  }

  return { time, open, high, low, close };
};

const extractCandles = (payload: unknown): CandlestickData[] => {
  const tryArrays = (value: unknown): unknown[] | null => {
    if (Array.isArray(value)) {
      return value;
    }

    if (!value || typeof value !== 'object') {
      return null;
    }

    for (const key of ['data', 'candles', 'klines', 'items']) {
      const nested = (value as Record<string, unknown>)[key];
      if (Array.isArray(nested)) {
        return nested;
      }
    }

    return null;
  };

  const array = tryArrays(payload) ??
    (payload && typeof payload === 'object'
      ? tryArrays((payload as Record<string, unknown>).result)
      : null);

  if (!array) {
    return [];
  }

  return array
    .map((item) => toCandlestickData(item))
    .filter((item): item is CandlestickData => item !== null)
    .sort((a, b) => Number(a.time) - Number(b.time));
};

const Chart: FC<{ candles: CandlestickData[] }> = ({ candles }) => {
  const containerRef = useRef<HTMLDivElement | null>(null);
  const chartRef = useRef<IChartApi | null>(null);
  const seriesRef = useRef<CandlestickSeries | null>(null);

  useEffect(() => {
    const element = containerRef.current;
    if (!element) {
      return;
    }

    const chart = createChart(element, {
      layout: {
        background: { color: '#010409' },
        textColor: '#e6edf3',
      },
      grid: {
        vertLines: { color: '#161b22' },
        horzLines: { color: '#161b22' },
      },
      rightPriceScale: {
        borderColor: '#30363d',
      },
      timeScale: {
        borderColor: '#30363d',
      },
      crosshair: {
        mode: 0,
      },
    });

    const series = chart.addCandlestickSeries({
      upColor: '#16a34a',
      downColor: '#dc2626',
      borderUpColor: '#22c55e',
      borderDownColor: '#ef4444',
      wickUpColor: '#22c55e',
      wickDownColor: '#ef4444',
    });

    chartRef.current = chart;
    seriesRef.current = series;

    const resize = () => {
      const { width, height } = element.getBoundingClientRect();
      chart.applyOptions({
        width: Math.max(width, 0),
        height: Math.max(height, 0),
      });
    };

    resize();

    const observer = new ResizeObserver((entries) => {
      const entry = entries[0];
      if (!entry) {
        return;
      }

      chart.applyOptions({
        width: Math.max(entry.contentRect.width, 0),
        height: Math.max(entry.contentRect.height, 0),
      });
    });

    observer.observe(element);

    return () => {
      observer.disconnect();
      chart.remove();
      chartRef.current = null;
      seriesRef.current = null;
    };
  }, []);

  useEffect(() => {
    if (!seriesRef.current) {
      return;
    }

    seriesRef.current.setData(candles);
    if (candles.length > 0) {
      chartRef.current?.timeScale().fitContent();
    }
  }, [candles]);

  return <div ref={containerRef} className="chart-container" />;
};

const App: React.FC = () => {
  const [candles, setCandles] = useState<CandlestickData[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const apiBaseUrl = useMemo(
    () => import.meta.env.VITE_API_BASE_URL?.replace(/\/$/, '') ?? 'http://localhost:8080',
    [],
  );

  useEffect(() => {
    const controller = new AbortController();

    const load = async () => {
      setLoading(true);
      setError(null);

      try {
        const response = await fetch(`${apiBaseUrl}${API_PATH}`, {
          signal: controller.signal,
        });

        if (!response.ok) {
          throw new Error(`Request failed with status ${response.status}`);
        }

        const payload = await response.json();
        const parsed = extractCandles(payload);

        if (parsed.length === 0) {
          throw new Error('No candle data available');
        }

        setCandles(parsed);
      } catch (err) {
        if ((err as Error).name === 'AbortError') {
          return;
        }

        const message = err instanceof Error ? err.message : 'Unknown error';
        setError(message);
      } finally {
        setLoading(false);
      }
    };

    void load();

    return () => controller.abort();
  }, [apiBaseUrl]);

  return (
    <div className="app">
      <header>
        <h1>BTC/USDT — 1 Minute Snapshot</h1>
        <p className="status">
          {loading && 'Loading latest candles…'}
          {!loading && !error && `Showing ${candles.length} candles from the snapshot endpoint.`}
          {error && <span className="error">Error loading candles: {error}</span>}
        </p>
      </header>
      <Chart candles={candles} />
    </div>
  );
};

export default App;
