#pragma once

#include <string>
#include <vector>

namespace ttp::common {
struct Config;
}

namespace app {

class BackfillWorker {
public:
    explicit BackfillWorker(const ttp::common::Config& config);

    void run();

private:
    std::string duckdbPath_;
    std::string exchange_;
    std::vector<std::string> symbols_;
    std::vector<std::string> intervals_;
    std::string from_;
    std::string to_;
};

}  // namespace app

