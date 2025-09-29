#pragma once

#include "config/Config.h"

#include <string>
#include <vector>

namespace config {

class ConfigProvider {
public:
    ConfigProvider(int argc, const char* const* argv);

    const Config& get() const { return cfg_; }

    static LogLevel parseLogLevel(const std::string& s);
    static std::string logLevelToString(LogLevel l);

private:
    Config cfg_;
    void parseCli_(int argc, const char* const* argv);
    void parseEnv_();
    void parseFile_(const std::string& path);

    static bool fileExists_(const std::string& path);
    static std::string trim_(const std::string& s);
    static bool parseBool_(const std::string& value, bool& out);
    static bool parseInt_(const std::string& value, int& out);
    static std::string lowercase_(std::string s);
};

}  // namespace config

