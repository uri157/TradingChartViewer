#pragma once

#include <string>

namespace bootstrap {

struct HybridConfig {
    std::string db_path;
    int         rest_port;
    int         ws_port;
    std::string default_symbol;
    std::string default_interval;
};

HybridConfig loadFromEnvOrFile();

}  // namespace bootstrap

