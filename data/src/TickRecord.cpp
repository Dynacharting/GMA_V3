#include "krn/data/TickRecord.hpp"

#include <cmath>

namespace krn::data {

// TickRecord.hpp's one out-of-line function. It moved here from the engine's
// src/data/SymbolTable.cpp with the header (ENC-1557).
std::int64_t toFixed(double px) noexcept {
  // Rounds half away from zero, matching std::llround. Providers that hand
  // over fixed point (Databento DBN is already 1e-9) must never come through
  // here — SPEC D8's "exact integer path with no double round trip" is about
  // precisely this function not being on the DBN path.
  return static_cast<std::int64_t>(std::llround(px * static_cast<double>(kPriceScale)));
}

} // namespace krn::data
