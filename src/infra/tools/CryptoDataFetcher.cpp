#define _CRT_SECURE_NO_WARNINGS
#include "infra/tools/CryptoDataFetcher.h"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/json.hpp>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include "logging/Log.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;
namespace json = boost::json;

namespace infra::tools {

CryptoDataFetcher::CryptoDataFetcher(std::string restHost)
    : restHost_(std::move(restHost)) {
    if (restHost_.empty()) {
        restHost_ = "api.binance.com";
    }
}

void CryptoDataFetcher::setRestHost(std::string host) {
    if (!host.empty()) {
        restHost_ = std::move(host);
    }
}

std::vector<infra::storage::PriceData> CryptoDataFetcher::fetchHistoricalData(const std::string& symbol,
                                                                             const std::string& interval,
                                                                             long long startTime,
                                                                             std::size_t limit) {
    std::vector<infra::storage::PriceData> all_data;

    try {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        auto const results = resolver.resolve(restHost_, "443");

        net::ssl::context ssl_ctx(net::ssl::context::sslv23_client);
        ssl_ctx.set_options(
            net::ssl::context::default_workarounds |
            net::ssl::context::no_sslv2 |
            net::ssl::context::no_sslv3 |
            net::ssl::context::single_dh_use
        );

        net::ssl::stream<tcp::socket> stream(ioc, ssl_ctx);
        if (!SSL_set_tlsext_host_name(stream.native_handle(), restHost_.c_str())) {
            beast::error_code ec{ static_cast<int>(::ERR_get_error()), net::error::get_ssl_category() };
            throw beast::system_error{ ec };
        }

        net::connect(stream.lowest_layer(), results.begin(), results.end());
        stream.handshake(net::ssl::stream_base::client);

        std::string target = "/api/v3/klines?symbol=" + symbol + "&interval=" + interval;
        target += "&startTime=" + std::to_string(startTime);
        if (limit == 0 || limit > 1000) {
            limit = 1000;
        }
        target += "&limit=" + std::to_string(limit);

        LOG_DEBUG(::logging::LogCategory::NET, "Request URL: %s", target.c_str());

        http::request<http::string_body> req{ http::verb::get, target, 11 };
        req.set(http::field::host, restHost_);
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::dynamic_body> res;
        http::read(stream, buffer, res);

        if (res.result() != http::status::ok) {
            LOG_WARN(::logging::LogCategory::NET, "HTTP error %d: %s", res.result_int(), res.reason().data());
            return all_data;
        }

        auto body = beast::buffers_to_string(res.body().data());
        json::value json_data = json::parse(body);

        if (!json_data.is_array()) {
            LOG_WARN(::logging::LogCategory::NET, "Unexpected response when fetching historical data: %s", body.c_str());
            return all_data;
        }

        for (const auto& item : json_data.as_array()) {
            if (!item.is_array()) continue;

            const auto& fields = item.as_array();
            if (fields.size() < 12) continue;

            infra::storage::PriceData data{};
            data.openTime = fields[0].as_int64();
            data.openPrice = std::stod(fields[1].as_string().c_str());
            data.highPrice = std::stod(fields[2].as_string().c_str());
            data.lowPrice = std::stod(fields[3].as_string().c_str());
            data.closePrice = std::stod(fields[4].as_string().c_str());
            data.volume = std::stod(fields[5].as_string().c_str());
            data.closeTime = fields[6].as_int64();
            data.baseAssetVolume = std::stod(fields[7].as_string().c_str());
            data.numberOfTrades = static_cast<int>(fields[8].as_int64());
            data.takerBuyVolume = std::stod(fields[9].as_string().c_str());
            data.takerBuyBaseAssetVolume = std::stod(fields[10].as_string().c_str());

            std::strncpy(data.symbol, symbol.c_str(), sizeof(data.symbol) - 1);
            data.symbol[sizeof(data.symbol) - 1] = '\0';

            std::strncpy(data.interval, interval.c_str(), sizeof(data.interval) - 1);
            data.interval[sizeof(data.interval) - 1] = '\0';

            all_data.emplace_back(data);
        }

        beast::error_code ec;
        stream.shutdown(ec);
        if (ec == net::error::eof) {
            ec = {};
        }
        if (ec) {
            LOG_DEBUG(::logging::LogCategory::NET, "SSL shutdown warning: %s", ec.message().c_str());
        }
    }
    catch (const std::exception& e) {
        LOG_WARN(::logging::LogCategory::NET, "Historical fetch exception: %s", e.what());
    }

    return all_data;
}

}  // namespace infra::tools
