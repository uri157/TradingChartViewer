#pragma once

#include <sstream>
#include <string>
#include <string_view>

namespace ttp::log {

enum class Level : int {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
};

void setLevel(Level level) noexcept;
Level getLevel() noexcept;
bool shouldLog(Level level) noexcept;
void log(Level level, const std::string& message);
const char* levelToString(Level level) noexcept;
Level levelFromString(std::string_view text);

}  // namespace ttp::log

#define TTP_LOG_IMPL(level, expr)                                                          \
    do {                                                                                   \
        if (::ttp::log::shouldLog(level)) {                                                \
            std::ostringstream ttp_log_stream__;                                           \
            ttp_log_stream__ << expr;                                                      \
            ::ttp::log::log(level, ttp_log_stream__.str());                                \
        }                                                                                  \
    } while (false)

#define LOG_DEBUG(expr) TTP_LOG_IMPL(::ttp::log::Level::Debug, expr)
#define LOG_INFO(expr) TTP_LOG_IMPL(::ttp::log::Level::Info, expr)
#define LOG_WARN(expr) TTP_LOG_IMPL(::ttp::log::Level::Warn, expr)
#define LOG_ERR(expr) TTP_LOG_IMPL(::ttp::log::Level::Error, expr)
