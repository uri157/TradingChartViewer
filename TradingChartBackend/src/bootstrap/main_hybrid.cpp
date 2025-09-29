#ifdef TTP_HYBRID_BACKEND

#include "bootstrap/Config.hpp"

#include <iostream>

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    const bootstrap::HybridConfig config = bootstrap::loadFromEnvOrFile();

    std::cout << "Hybrid backend configuration:\n"
              << "  db_path: " << config.db_path << '\n'
              << "  rest_port: " << config.rest_port << '\n'
              << "  ws_port: " << config.ws_port << '\n'
              << "  default_symbol: " << config.default_symbol << '\n'
              << "  default_interval: " << config.default_interval << '\n';

    return 0;
}

#endif  // TTP_HYBRID_BACKEND

