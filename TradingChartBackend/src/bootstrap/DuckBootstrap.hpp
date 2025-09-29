#pragma once

#include <memory>

namespace core::app {
class ICandleRepository;
}

namespace bootstrap {

std::unique_ptr<core::app::ICandleRepository> makeDuckRepo();

}  // namespace bootstrap

