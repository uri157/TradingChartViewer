# syntax=docker/dockerfile:1.5

# ---------- Builder ----------
    FROM ubuntu:24.04 AS builder
    ENV DEBIAN_FRONTEND=noninteractive
    
    RUN apt-get update \
      && apt-get install -y --no-install-recommends \
         build-essential cmake pkg-config \
         libssl-dev \
         libboost-dev libboost-system-dev libboost-json-dev \
         ca-certificates \
      && rm -rf /var/lib/apt/lists/*
    
    WORKDIR /src
    COPY . .
    
    # Limpio y build de release SIN sanitizers
    RUN make clean || true
    RUN make APP=api MODE=release SANITIZE=0 DUCKDB_VENDOR=1 GLIBCXX_ABI=0
    RUN strip bin/api
    
    # Verificación: no debe depender de ASan
    RUN if ldd bin/api | grep -iq asan; then echo "ERROR: bin/api linkeado a ASan"; exit 1; fi
    
    # ---------- Runtime ----------
    FROM ubuntu:24.04 AS runtime
    ENV DEBIAN_FRONTEND=noninteractive
    
    RUN apt-get update \
      && apt-get install -y --no-install-recommends \
         ca-certificates \
         libssl3 \
         libstdc++6 \
         libboost-system1.83.0 libboost-json1.83.0 \
         curl \
      && rm -rf /var/lib/apt/lists/*
    
    # Usuario no-root y dirs
    RUN useradd --system --create-home --home /home/app --shell /usr/sbin/nologin app \
      && mkdir -p /app/bin /app/third_party/duckdb/lib /data \
      && chown -R app:app /app /data
    
    # Binario + DuckDB vendorizado
    COPY --from=builder --chown=app:app /src/bin/api /app/bin/api
    COPY --from=builder --chown=app:app /src/third_party/duckdb/lib /app/third_party/duckdb/lib
    
    ENV LD_LIBRARY_PATH="/app/third_party/duckdb/lib:${LD_LIBRARY_PATH}"
    
    USER app
    WORKDIR /app
    VOLUME ["/data"]
    EXPOSE 8080
    
    ENTRYPOINT ["/app/bin/api"]
    CMD ["--live=1", "--storage", "duck", "--duckdb", "/data/market.duckdb", "--log-level", "info"]
    