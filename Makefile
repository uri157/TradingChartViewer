# =========================
# Build config
# =========================
TARGET   := bin/main
SRC_DIR  := src
OBJ_DIR  := obj
BIN_DIR  := bin

CXX      := g++
MODE     ?= debug           # debug | release
SANITIZE ?=                 # (vacío) -> usa address,undefined en debug
WERROR   ?= 0               # 1 para -Werror

WARNINGS := -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wduplicated-cond -Wlogical-op
CXXFLAGS := -std=c++17 -MMD -MP -pthread $(WARNINGS) -Isrc
SANITIZE_FLAGS :=

# =========================
# Optimizaciones / Sanitizers
# =========================
ifeq ($(MODE),release)
  CXXFLAGS += -O3 -DNDEBUG
  SANITIZE :=
else
  CXXFLAGS += -O0 -g -fno-omit-frame-pointer
  ifeq ($(strip $(SANITIZE)),)
    SANITIZE := address,undefined
  endif
endif

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
# Fuentes (recursivo con exclusiones)
# =========================
EXCLUDE_DIRS := $(SRC_DIR)/_legacy $(SRC_DIR)/unused $(SRC_DIR)/_linter
FIND_EXCLUDES := $(foreach d,$(EXCLUDE_DIRS),-path '$(d)' -prune -o)

SRC  := $(shell find $(SRC_DIR) $(FIND_EXCLUDES) -name '*.cpp' -print)
OBJ  := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRC))
DEPS := $(OBJ:.o=.d)

# =========================
# Librerías (SFML, OpenSSL, Boost)
# =========================
SFML_PKGS    := sfml-graphics sfml-window sfml-system
SFML_CFLAGS  := $(shell pkg-config --cflags $(SFML_PKGS) 2>/dev/null)
SFML_LDFLAGS := $(shell pkg-config --libs   $(SFML_PKGS) 2>/dev/null)
ifeq ($(strip $(SFML_LDFLAGS)),)
  SFML_LDFLAGS := -lsfml-graphics -lsfml-window -lsfml-system
endif

OPENSSL_LDFLAGS := $(shell pkg-config --libs openssl 2>/dev/null)
ifeq ($(strip $(OPENSSL_LDFLAGS)),)
  OPENSSL_LDFLAGS := -lssl -lcrypto
endif

LIBS := -lboost_system -lboost_json

# =========================
# Reglas
# =========================
$(TARGET): $(OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ) $(SFML_LDFLAGS) $(OPENSSL_LDFLAGS) $(LIBS) $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(SFML_CFLAGS) -c $< -o $@

$(BIN_DIR) $(OBJ_DIR):
	@mkdir -p $@

# Utilidades
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

list:
	@printf "%s\n" $(SRC)

print-%:
	@echo '$*=$($*)'

.PHONY: clean list print-%

# Dependencias
-include $(DEPS)
