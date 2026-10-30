// Fixed-point price scale (ENC-1481). Split out of SymbolTableTest.cpp by
// ENC-1558: SymbolTable stays in the engine, but kPriceScale and toFixed are
// krn_data's (krn/data/TickRecord.hpp), so these cases stay in krn_data_tests.
//
// Design: specs/2026-10-14-krn-data-layer/SPEC.md D8.

#include "krn/data/TickRecord.hpp"

#include <gtest/gtest.h>

#include <cstdint>

// Carried from SymbolTableTest's ZeroIsReservedAndNeverIssued: a
// default-constructed header names no symbol and no venue.
static_assert(krn::data::RecordHeader{}.symbolId == krn::data::kInvalidSymbolId);
static_assert(krn::data::RecordHeader{}.venueId == krn::data::kInvalidSymbolId);

// SPEC D8: the fixed global 1e-9 scale.
TEST(FixedPointTest, ScaleIsGlobalAndExact) {
  EXPECT_EQ(krn::data::kPriceScale, 1'000'000'000LL);
  EXPECT_EQ(krn::data::kNoPrice, INT64_MIN);
  EXPECT_EQ(krn::data::toFixed(1.0), 1'000'000'000LL);
  EXPECT_EQ(krn::data::toFixed(-1.5), -1'500'000'000LL);
  EXPECT_EQ(krn::data::toFixed(0.0), 0LL);
  EXPECT_DOUBLE_EQ(krn::data::toDouble(1'234'567'890LL), 1.23456789);
}

// WHY SPEC D8 insists on "an exact integer path from DBN with no double round
// trip". A 1e-9-scaled price past 2^53 cannot survive a double, so a
// normalizer that routed DBN's own fixed point through toDouble/toFixed would
// corrupt large prices silently. The second half of this test is the reason it
// would not show up in a smoke test: an ordinary equity price round-trips
// perfectly, so the bug is invisible until it is not.
TEST(FixedPointTest, DoubleRoundTripIsLossyPastTwoToThe53) {
  const std::int64_t beyondDouble = 9'007'199'254'740'993LL;  // 2^53 + 1
  EXPECT_NE(krn::data::toFixed(krn::data::toDouble(beyondDouble)), beyondDouble);

  const std::int64_t aapl = 190'125'000'000LL;  // $190.125 at 1e-9
  EXPECT_EQ(krn::data::toFixed(krn::data::toDouble(aapl)), aapl);
}
