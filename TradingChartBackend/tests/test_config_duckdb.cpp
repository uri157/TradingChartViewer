#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "common/Config.hpp"

namespace {

struct DuckdbEnvGuard {
    DuckdbEnvGuard() {
        const char* current = std::getenv("DUCKDB_PATH");
        if (current) {
            originalValue = current;
            hadOriginal = true;
        }
    }

    ~DuckdbEnvGuard() {
        if (hadOriginal) {
            ::setenv("DUCKDB_PATH", originalValue.c_str(), 1);
        } else {
            ::unsetenv("DUCKDB_PATH");
        }
    }

    void clear() { ::unsetenv("DUCKDB_PATH"); }

    void set(const std::string& value) { ::setenv("DUCKDB_PATH", value.c_str(), 1); }

    bool hadOriginal{false};
    std::string originalValue;
};

::ttp::common::Config runConfig(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    return ::ttp::common::Config::fromArgs(static_cast<int>(argv.size()), argv.data());
}

}  // namespace

int main() {
    DuckdbEnvGuard envGuard;
    const std::string flagPath = "/tmp/thetradingviewer/flag/market.duckdb";
    const std::string envPath = "/tmp/thetradingviewer/env/market.duckdb";

    // Default when env and flag are absent.
    envGuard.clear();
    auto configDefault = runConfig({"app"});
    if (configDefault.duckdbPath != "/data/market.duckdb") {
        std::cerr << "Expected default DuckDB path to be /data/market.duckdb but got "
                  << configDefault.duckdbPath << "\n";
        return 1;
    }
    const auto defaultParent = std::filesystem::path(configDefault.duckdbPath).parent_path();
    if (!defaultParent.empty() && std::filesystem::exists(defaultParent) && std::filesystem::is_empty(defaultParent)) {
        std::filesystem::remove(defaultParent);
    }

    // Environment variable overrides default.
    envGuard.set(envPath);
    std::filesystem::remove_all(std::filesystem::path(envPath).parent_path());
    auto configEnv = runConfig({"app"});
    if (configEnv.duckdbPath != envPath) {
        std::cerr << "Expected env DuckDB path to be " << envPath << " but got " << configEnv.duckdbPath
                  << "\n";
        return 1;
    }
    if (!std::filesystem::exists(std::filesystem::path(envPath).parent_path())) {
        std::cerr << "Expected parent directory for env path to be created\n";
        return 1;
    }
    std::filesystem::remove_all(std::filesystem::path(envPath).parent_path());

    // CLI flag overrides environment variable.
    envGuard.set(envPath);
    std::filesystem::remove_all(std::filesystem::path(flagPath).parent_path());
    auto configFlag = runConfig({"app", "--duckdb", flagPath});
    if (configFlag.duckdbPath != flagPath) {
        std::cerr << "Expected flag DuckDB path to be " << flagPath << " but got " << configFlag.duckdbPath
                  << "\n";
        return 1;
    }
    if (!std::filesystem::exists(std::filesystem::path(flagPath).parent_path())) {
        std::cerr << "Expected parent directory for flag path to be created\n";
        return 1;
    }
    std::filesystem::remove_all(std::filesystem::path(flagPath).parent_path());

    return 0;
}
