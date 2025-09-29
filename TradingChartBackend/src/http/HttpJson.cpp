#include "http/HttpJson.hpp"

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

}  // namespace

namespace ttp::http {

namespace {

std::string serialize_json(const boost::json::value& value) {
    boost::json::serializer sr;
    sr.reset(&value);

    std::string result;
    std::array<char, 4096> buffer{};

    while (!sr.done()) {
        boost::json::string_view chunk = sr.read(buffer.data(), buffer.size());
        result.append(chunk.data(), chunk.size());
    }

    return result;
}

std::string serialize_json(const boost::json::object& object) {
    return serialize_json(boost::json::value{object});
}

}  // namespace

void write_json(ttp::api::Response& response, const boost::json::value& value) {
    response.body = serialize_json(value);
    response.statusCode = 200;
    response.statusText = status_reason(response.statusCode);
    response.contentType = "application/json; charset=utf-8";
    response.headers.clear();
}

}  // namespace ttp::http

