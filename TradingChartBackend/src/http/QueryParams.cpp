#include "http/QueryParams.hpp"

#include <charconv>
#include <optional>
#include <string>
#include <string_view>

namespace {

std::string decode_component(std::string_view value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char ch = value[i];
        if (ch == '+') {
            decoded.push_back(' ');
        }
        else if (ch == '%' && i + 2 < value.size()) {
            const char* begin = value.data() + i + 1;
            const char* end = begin + 2;
            unsigned int code{};
            auto [ptr, ec] = std::from_chars(begin, end, code, 16);
            if (ec == std::errc() && ptr == end) {
                decoded.push_back(static_cast<char>(code));
                i += 2;
            }
            else {
                decoded.push_back(ch);
            }
        }
        else {
            decoded.push_back(ch);
        }
    }
    return decoded;
}

std::optional<std::string> find_query_value(const std::string& query, std::string_view key) {
    const std::string key_str(key);
    std::size_t start = 0;
    while (start <= query.size()) {
        const auto end = query.find('&', start);
        const auto part = query.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const auto eq = part.find('=');
        std::string raw_key;
        std::string raw_value;
        if (eq == std::string::npos) {
            raw_key = part;
        }
        else {
            raw_key = part.substr(0, eq);
            raw_value = part.substr(eq + 1);
        }
        if (decode_component(raw_key) == key_str) {
            return decode_component(raw_value);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return std::nullopt;
}

}  // namespace

namespace ttp::http {

std::optional<std::string> opt_string(const ttp::api::Request& request, const char* key) {
    if (!key) {
        return std::nullopt;
    }
    return find_query_value(request.query, key);
}

std::optional<int> opt_int(const ttp::api::Request& request, const char* key) {
    const auto value = opt_string(request, key);
    if (!value || value->empty()) {
        return std::nullopt;
    }
    int result = 0;
    const auto* begin = value->data();
    const auto* end = begin + value->size();
    auto [ptr, ec] = std::from_chars(begin, end, result);
    if (ec != std::errc() || ptr != end) {
        return std::nullopt;
    }
    return result;
}

std::optional<std::int64_t> opt_int64(const ttp::api::Request& request, const char* key) {
    const auto value = opt_string(request, key);
    if (!value || value->empty()) {
        return std::nullopt;
    }
    std::int64_t result = 0;
    const auto* begin = value->data();
    const auto* end = begin + value->size();
    auto [ptr, ec] = std::from_chars(begin, end, result);
    if (ec != std::errc() || ptr != end) {
        return std::nullopt;
    }
    return result;
}

}  // namespace ttp::http

