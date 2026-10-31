// tests/feed/GapFixesTest.cpp
//
// Tests for the 7 gap fixes:
//   Fix 1: Adapter-controlled subscribe message
//   Fix 2: Multi-feed config parsing (legacy keys; left the engine, ENC-1574)
//   Fix 3: Configurable history limits
//   Fix 4: Opt-in TA computation
//   Fix 5+7: Richer TickEntry + timestamps
//   Fix 6: Allow negative prices in OrderBook
//
// Fixes 6, 1, 5+7 and 4 need the market connector and are in
// krn-common's tests/market/GapFixesTest.cpp (krn_common_tests).

#include <gtest/gtest.h>

#include "krn/util/Config.hpp"
#include "krn/Dispatcher.hpp"
#include "krn/AtomicStore.hpp"
#include "krn/FunctionRegistry.hpp"
#include "krn/Event.hpp"
#include "krn/StreamValue.hpp"
#include "krn/rt/ThreadPool.hpp"
#include "krn/nodes/INode.hpp"

#include <rapidjson/document.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace krn;

// ===========================================================================
// Helpers
// ===========================================================================

namespace {

static Event makeTick(const std::string& symbol,
                           std::initializer_list<std::pair<const char*, double>> fields) {
    auto doc = std::make_shared<rapidjson::Document>();
    doc->SetObject();
    auto& a = doc->GetAllocator();
    for (auto& [k, v] : fields) {
        doc->AddMember(rapidjson::Value(k, a), rapidjson::Value(v), a);
    }
    return Event{symbol, std::move(doc)};
}

} // anonymous namespace

// ===========================================================================
// Fix 3: Configurable history limits
// ===========================================================================

TEST(GapFixesTest, ConfigDefaultHistoryLimits) {
    util::Config cfg;
    EXPECT_EQ(cfg.taHistoryMax, 1000);
    EXPECT_EQ(cfg.maxSymbols, 10000);
    EXPECT_EQ(cfg.maxFieldsPerSymbol, 200);
}

TEST(GapFixesTest, MaxSymbolsLimitRejectsExcessSymbols) {
    registerBuiltinFunctions();
    rt::ThreadPool pool(1);
    AtomicStore store;
    util::Config cfg;
    cfg.maxSymbols = 2;

    Dispatcher md(&pool, &store, cfg);

    // Send ticks for 3 symbols; the 3rd should be rejected by the symbol cap.
    md.onTick(makeTick("SYM_A", {{"lastPrice", 10.0}, {"volume", 1.0}}));
    md.onTick(makeTick("SYM_B", {{"lastPrice", 20.0}, {"volume", 1.0}}));
    md.onTick(makeTick("SYM_C", {{"lastPrice", 30.0}, {"volume", 1.0}}));
    pool.shutdown();

    // First two symbols should have their data in the store.
    EXPECT_TRUE(store.get("SYM_A", "lastPrice").has_value());
    EXPECT_TRUE(store.get("SYM_B", "lastPrice").has_value());

    // The third symbol should NOT have data — it was rejected.
    EXPECT_FALSE(store.get("SYM_C", "lastPrice").has_value());
}
