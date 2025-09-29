#pragma once

#ifdef TTP_HYBRID_BACKEND
constexpr bool kHybridBackend = true;
#else
constexpr bool kHybridBackend = false;
#endif
