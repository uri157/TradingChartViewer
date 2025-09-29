#include "http/json_error.hpp"

#include <array>
#include <string>

#include <boost/json/object.hpp>
#include <boost/json/serializer.hpp>

namespace {

std::string status_reason(int statusCode) {
    switch (statusCode) {
    case 200:
        return "OK";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 500:
        return "Internal Server Error";
    default:
        break;
    }
    return "Unknown";
}

std::string serialize_json(const boost::json::object& object) {
    boost::json::value value{object};
    boost::json::serializer serializer;
    serializer.reset(&value);

    std::string result;
    std::array<char, 4096> buffer{};

    while (!serializer.done()) {
        boost::json::string_view chunk = serializer.read(buffer.data(), buffer.size());
        result.append(chunk.data(), chunk.size());
    }

    return result;
}

}  // namespace

namespace ttp::http {

void json_error(ttp::api::Response& response, int statusCode, std::string_view errorCode) {
    boost::json::object payload;
    payload["error"] = errorCode;

    response.body = serialize_json(payload);
    response.statusCode = statusCode;
    response.statusText = status_reason(statusCode);
    response.contentType = "application/json; charset=utf-8";
    response.headers.clear();
}

}  // namespace ttp::http

