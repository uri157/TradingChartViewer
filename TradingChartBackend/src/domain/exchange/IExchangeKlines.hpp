#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "domain/Types.h"

namespace domain {

struct KlinesPage {
  std::vector<Candle> rows;
  bool has_more{false};
  std::int64_t next_from_ts{0};
};

class IExchangeKlines {
 public:
  virtual ~IExchangeKlines() = default;
  virtual KlinesPage fetch_klines(const std::string& symbol,
                                  Interval interval,
                                  std::int64_t from_ts,
                                  std::int64_t to_ts,
                                  std::size_t page_limit = 1000) = 0;
};

class IExchangeLiveKlines {
 public:
  virtual ~IExchangeLiveKlines() = default;
  virtual void subscribe(
      const std::vector<std::string>& symbols,
      Interval interval,
      std::function<void(const std::string&, const Candle&)> on_closed_candle) =
      0;
  virtual void set_on_reconnected(std::function<void()> callback) = 0;
  virtual void stop() = 0;
};

std::string to_string(Interval i);
Interval interval_from_string(const std::string&);

}  // namespace domain
