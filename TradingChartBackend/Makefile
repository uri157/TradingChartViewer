# =========================
# Build config
# =========================
APP      ?= main               # main | api | hybrid
SRC_DIR  := src
OBJ_DIR  := obj
BIN_DIR  := bin

CXX      := g++
MODE     ?= debug              # debug | release
SANITIZE ?=                    # vacío -> address,undefined SOLO en debug
WERROR   ?= 0                  # 1 para -Werror
HYBRID_BACKEND ?= 0

WARNINGS := -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wduplicated-cond -Wlogical-op
CXXFLAGS := -std=c++17 -MMD -MP -pthread $(WARNINGS) -Isrc -Ivendor -DTTP_ENABLE_DIAG=1
SANITIZE_FLAGS :=
LDFLAGS   :=

ifeq ($(strip $(HYBRID_BACKEND)),1)
  CXXFLAGS += -DTTP_HYBRID_BACKEND=1
endif

# =========================
# Optimizaciones / Sanitizers
# =========================
# Normalización de "desactivar sanitizers" por variable de entorno
ifeq ($(strip $(SANITIZE)),0)
  SANITIZE :=
endif
ifeq ($(strip $(SANITIZE)),none)
  SANITIZE :=
endif

ifeq ($(MODE),release)
  CXXFLAGS += -O3 -DNDEBUG
  # En release: SIEMPRE desactivar sanitizers
  SANITIZE :=
else
  CXXFLAGS += -O0 -g -fno-omit-frame-pointer
  ifeq ($(strip $(SANITIZE)),)
    # Solo en debug, si no se pasa SANITIZE, activamos address+undefined
    SANITIZE := address,undefined
  endif
endif

# Expandir banderas de sanitizers sólo si están activas
ifneq ($(strip $(SANITIZE)),)
  ifneq ($(findstring address,$(SANITIZE)),)
    SANITIZE_FLAGS += -fsanitize=address
    CXXFLAGS += -DTTP_WITH_ASAN=1
  endif
  ifneq ($(findstring undefined,$(SANITIZE)),)
    SANITIZE_FLAGS += -fsanitize=undefined
    CXXFLAGS += -DTTP_WITH_UBSAN=1
  endif
endif

ifneq ($(strip $(SANITIZE_FLAGS)),)
  CXXFLAGS += $(SANITIZE_FLAGS)
  LDFLAGS  += $(SANITIZE_FLAGS)
endif

ifneq ($(strip $(WERROR)),0)
  CXXFLAGS += -Werror
endif

# =========================
# ABI libstdc++ (explícito)
# =========================
GLIBCXX_ABI ?= 0
CXXFLAGS += -D_GLIBCXX_USE_CXX11_ABI=$(GLIBCXX_ABI)

# =========================
# Fuentes (recursivo con exclusiones)
# =========================
EXCLUDE_DIRS := $(SRC_DIR)/_legacy $(SRC_DIR)/unused $(SRC_DIR)/_linter
FIND_EXCLUDES := $(foreach d,$(EXCLUDE_DIRS),-path '$(d)' -prune -o)

SRC  := $(shell find $(SRC_DIR) $(FIND_EXCLUDES) -name '*.cpp' -print)
SRC  := $(filter-out $(SRC_DIR)/tools/importer.cpp,$(SRC))

ifeq ($(APP),api)
  TARGET := $(BIN_DIR)/api
  EXCLUDE_SRCS += $(SRC_DIR)/bootstrap/main.cpp $(SRC_DIR)/bootstrap/main_hybrid.cpp
else ifeq ($(APP),hybrid)
  TARGET := $(BIN_DIR)/hybrid
  EXCLUDE_SRCS += $(SRC_DIR)/bootstrap/main.cpp $(SRC_DIR)/main_api.cpp
else
  TARGET := $(BIN_DIR)/main
  EXCLUDE_SRCS += $(SRC_DIR)/bootstrap/main_hybrid.cpp $(SRC_DIR)/main_api.cpp
endif

ifneq ($(strip $(EXCLUDE_SRCS)),)
  SRC := $(filter-out $(EXCLUDE_SRCS),$(SRC))
endif

OBJ  := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRC))
DEPS := $(OBJ:.o=.d)

# =========================
# Librerías (Boost, OpenSSL, DuckDB)
# =========================
PKG_CONFIG ?= pkg-config
HAVE_PKG_CONFIG := $(shell command -v $(PKG_CONFIG) >/dev/null 2>&1 && echo 1 || echo 0)

# --- Boost
LIBS += -lboost_system

# --- OpenSSL
OPENSSL_CFLAGS :=
OPENSSL_LDFLAGS :=
ifeq ($(HAVE_PKG_CONFIG),1)
  OPENSSL_CFLAGS  := $(shell $(PKG_CONFIG) --cflags openssl 2>/dev/null)
  OPENSSL_LDFLAGS := $(shell $(PKG_CONFIG) --libs   openssl 2>/dev/null)
endif
ifeq ($(strip $(OPENSSL_LDFLAGS)),)
  OPENSSL_LDFLAGS := -lssl -lcrypto
endif
CXXFLAGS += $(OPENSSL_CFLAGS)
LIBS     += $(OPENSSL_LDFLAGS)

# --- DuckDB (pkg-config | sistema | vendor binario)
DUCKDB_VENDOR ?= 0

DUCKDB_VENDOR_INC := third_party/duckdb/include
DUCKDB_VENDOR_LIB := third_party/duckdb/lib
DUCKDB_VENDOR_H   := $(DUCKDB_VENDOR_INC)/duckdb.h
DUCKDB_VENDOR_SO  := $(DUCKDB_VENDOR_LIB)/libduckdb.so

HAS_DUCKDB := 0
DUCKDB_LDFLAGS :=

# 1) pkg-config
ifeq ($(strip $(DUCKDB_VENDOR)),0)
  ifeq ($(HAVE_PKG_CONFIG),1)
    ifeq ($(shell $(PKG_CONFIG) --exists duckdb && echo 1 || echo 0),1)
      CXXFLAGS       += $(shell $(PKG_CONFIG) --cflags duckdb)
      DUCKDB_LDFLAGS += -Wl,--no-as-needed $(shell $(PKG_CONFIG) --libs duckdb) -Wl,--as-needed
      HAS_DUCKDB := 1
    endif
  endif
endif

# 2) lib del sistema
ifeq ($(HAS_DUCKDB),0)
  ifeq ($(strip $(DUCKDB_VENDOR)),0)
    DUCKDB_SYS_LIB := $(firstword $(wildcard /usr/lib/libduckdb.* /usr/local/lib/libduckdb.*))
    ifneq ($(strip $(DUCKDB_SYS_LIB)),)
      DUCKDB_LDFLAGS += -Wl,--no-as-needed -lduckdb -Wl,--as-needed
      HAS_DUCKDB := 1
    endif
  endif
endif

# 3) vendor binario
ifeq ($(HAS_DUCKDB),0)
  ifneq ($(wildcard $(DUCKDB_VENDOR_H))$(wildcard $(DUCKDB_VENDOR_SO)),)
    CXXFLAGS       += -I$(DUCKDB_VENDOR_INC)
    DUCKDB_LDFLAGS += -Wl,--no-as-needed -L$(DUCKDB_VENDOR_LIB) -lduckdb -Wl,--as-needed
    LDFLAGS        += -Wl,-rpath,'$$ORIGIN/../third_party/duckdb/lib'
    HAS_DUCKDB := 1
  endif
endif

ifeq ($(HAS_DUCKDB),1)
  CXXFLAGS += -DHAS_DUCKDB=1
endif

LIBS += $(DUCKDB_LDFLAGS)

# Asegurar Boost.JSON al final del set de LIBS (necesario para los símbolos de json)
LIBS += -lboost_json

# =========================
# Reglas
# =========================
$(TARGET): $(OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN_DIR) $(OBJ_DIR):
	@mkdir -p $@

# =========================
# Utilidades
# =========================
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

# ---------- Docker / Compose ----------
compose_base := docker-compose.yml
compose_dev  := docker-compose.dev.yml
compose_prod := docker-compose.prod.yml

# Si existen UID/GID de la shell se usan; si no, intenta resolverlos
UID ?= $(shell id -u 2>/dev/null || echo 1000)
GID ?= $(shell id -g 2>/dev/null || echo 1000)

docker-build:
	docker build -t ttv/api:local .

up: ## up con sólo el compose base (no recomendado si querés persistencia)
	docker compose -f $(compose_base) up -d

down:
	docker compose -f $(compose_base) down

logs:
	docker compose -f $(compose_base) logs -f api

# Desarrollo: bind mount + user mapping
up-dev:
	UID=$(UID) GID=$(GID) docker compose -f $(compose_base) -f $(compose_dev) up -d

down-dev:
	docker compose -f $(compose_base) -f $(compose_dev) down

logs-dev:
	docker compose -f $(compose_base) -f $(compose_dev) logs -f api

# Producción: named volume
up-prod:
	docker compose -f $(compose_base) -f $(compose_prod) up -d

down-prod:
	docker compose -f $(compose_base) -f $(compose_prod) down

logs-prod:
	docker compose -f $(compose_base) -f $(compose_prod) logs -f api

# ---------- Ejecutar local ----------
run-api:
	$(MAKE) APP=api MODE=$(MODE) $(BIN_DIR)/api
	./bin/api --log-level debug

# ---- Backfill helper en contenedor (opcional) ----
BACKFILL_EXCHANGE ?= binance
BACKFILL_LIVE_SYMBOLS ?= BTCUSDT,ETHUSDT
BACKFILL_LIVE_INTERVALS ?= 1m,1d
BACKFILL_LOG_LEVEL ?= info
BACKFILL_SYMBOLS ?=
BACKFILL_INTERVALS ?=
BACKFILL_FROM ?=
BACKFILL_TO ?=
BACKFILL_MODE ?=
BACKFILL_LOOKBACK_MAX ?=
BACKFILL_CHUNK ?=
BACKFILL_CONCURRENCY ?=

BACKFILL_OPTIONAL_FLAGS :=
BACKFILL_OPTIONAL_FLAGS += $(if $(BACKFILL_SYMBOLS),--symbols "$(BACKFILL_SYMBOLS)")
BACKFILL_OPTIONAL_FLAGS += $(if $(BACKFILL_INTERVALS),--intervals "$(BACKFILL_INTERVALS)")
BACKFILL_OPTIONAL_FLAGS += $(if $(BACKFILL_FROM),--from "$(BACKFILL_FROM)")
BACKFILL_OPTIONAL_FLAGS += $(if $(BACKFILL_TO),--to "$(BACKFILL_TO)")
BACKFILL_OPTIONAL_FLAGS += $(if $(BACKFILL_MODE),--backfill-mode "$(BACKFILL_MODE)")
BACKFILL_OPTIONAL_FLAGS += $(if $(BACKFILL_LOOKBACK_MAX),--lookback-max "$(BACKFILL_LOOKBACK_MAX)")
BACKFILL_OPTIONAL_FLAGS += $(if $(BACKFILL_CHUNK),--backfill-chunk "$(BACKFILL_CHUNK)")
BACKFILL_OPTIONAL_FLAGS += $(if $(BACKFILL_CONCURRENCY),--backfill-concurrency "$(BACKFILL_CONCURRENCY)")

backfill:
	@mkdir -p data
	docker run --rm \
	  -v $(CURDIR)/data:/data \
	  ttv/api:local \
	  ./bin/api \
	  --live=0 \
	  --storage duck \
	  --duckdb /data/market.duckdb \
	  --exchange $(BACKFILL_EXCHANGE) \
	  --live-symbols "$(BACKFILL_LIVE_SYMBOLS)" \
	  --live-intervals "$(BACKFILL_LIVE_INTERVALS)" \
	  --log-level $(BACKFILL_LOG_LEVEL) \
	  --backfill$(if $(strip $(BACKFILL_OPTIONAL_FLAGS)), $(BACKFILL_OPTIONAL_FLAGS))

list:
	@printf "%s\n" $(SRC)

print-%:
	@echo '$*=$($*)'

.PHONY: clean run-api backfill list print-% docker-build up down logs up-dev down-dev logs-dev up-prod down-prod logs-prod
-include $(DEPS)
