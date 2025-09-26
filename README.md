##ubuntu

sudo apt update
sudo apt install -y build-essential g++ make \
  libsfml-dev \
## Build targets

```sh
make -j"$(nproc)"              # Debug build with warnings and -g
make SANITIZE=address -j        # Enable ASAN instrumentation
make SANITIZE=address,undefined -j
make MODE=release -j            # Optimized build without sanitizers
make WERROR=1 -j                # Optional: promote warnings to errors
```

Sanitizers are only available in the default (debug) profile. Use `MODE=release` for `-O3 -DNDEBUG` builds.

## Runtime logging

* Default level: `INFO`.
* CLI flags:
  * `--log-level=<trace|debug|info|warn|error>`
  * `--trace` (forces `TRACE` level)
  * `--debug` (forces `DEBUG` level)
* Environment override: `TTP_LOG_LEVEL=trace|debug|info|warn|error`.
* Priority: CLI level > `--trace/--debug` > `TTP_LOG_LEVEL` > default.

Example commands:

```sh
./bin/main
TTP_LOG_LEVEL=debug ./bin/main
./bin/main --trace
ASAN_OPTIONS=abort_on_error=1:symbolize=1 ./bin/main --trace
```

At startup the application prints the effective level and sanitizer configuration.

  libssl-dev \
  libboost-system-dev libboost-json-dev

make clean
make -j"$(nproc)"


./bin/main


ASAN_OPTIONS=abort_on_error=1:symbolize=1:detect_leaks=1 ./bin/main


