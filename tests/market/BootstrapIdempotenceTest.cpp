// BootstrapIdempotenceTest, lifted from tests/engine/RegistriesTest.cpp by
// ENC-1562: it pins the MARKET bootstrap (tests/market/test_bootstrap.cpp),
// so it runs in krn_market_tests.

#include "krn/engine/EventComputerRegistry.hpp"

#include <gtest/gtest.h>

using namespace krn::engine;

// ---------- Bootstrap idempotence (ENC-1102) ----------
//
// MarketConnector::registerWith() APPENDS its "tick" computer factory to the
// process-global EventComputerRegistry (there is no per-factory removal), so
// running the test bootstrap twice gives every Dispatcher two
// MarketTickComputers per tick, each with its own TA history — silent
// double-computation across the whole suite rather than a failing assertion.
// gtest only re-runs a global Environment between --gtest_repeat iterations
// under --gtest_recreate_environments_when_repeating, so this is one flag away
// by default. Nothing else in the binary registers a "tick" factory, so the
// count is an order-independent invariant.

TEST(BootstrapIdempotenceTest, TickComputerFactoryRegisteredExactlyOnce) {
  EXPECT_EQ(EventComputerRegistry::factoryCount("tick"), 1u)
      << "test bootstrap re-registered the market tick computer; every "
         "Dispatcher in this iteration computes each tick's atomics more than "
         "once";
}
