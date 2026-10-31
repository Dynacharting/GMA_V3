// tests/feed/GapFixesTest.cpp
//
// Tests for the 7 gap fixes:
//   Fix 1: Adapter-controlled subscribe message
//   Fix 2: Multi-feed config parsing
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

// ===========================================================================
// Fix 2: Multi-feed config parsing
// ===========================================================================

TEST(GapFixesTest, MultiFeedConfigParsing) {
    const char* path = "test_gap_multifeed.ini";
    {
        std::ofstream f(path);
        f << "feed.0.url=wss://feed1.example.com/ws\n"
          << "feed.0.adapter=itch\n"
          << "feed.0.symbols=AAPL,MSFT,GOOG\n"
          << "feed.1.url=wss://feed2.example.com/ws\n"
          << "feed.1.adapter=generic\n"
          << "feed.1.symbols=BTC,ETH\n";
    }

    util::Config cfg;
    EXPECT_TRUE(cfg.loadFromFile(path));

    ASSERT_EQ(cfg.feeds.size(), 2u);

    EXPECT_EQ(cfg.feeds[0].url, "wss://feed1.example.com/ws");
    EXPECT_EQ(cfg.feeds[0].adapter, "itch");
    EXPECT_EQ(cfg.feeds[0].symbols, (std::vector<std::string>{"AAPL", "MSFT", "GOOG"}));

    EXPECT_EQ(cfg.feeds[1].url, "wss://feed2.example.com/ws");
    EXPECT_EQ(cfg.feeds[1].adapter, "generic");
    EXPECT_EQ(cfg.feeds[1].symbols, (std::vector<std::string>{"BTC", "ETH"}));

    std::remove(path);
}

TEST(GapFixesTest, LegacyFeedFallbackWhenFeedsEmpty) {
    const char* path = "test_gap_legacy_feed.ini";
    {
        std::ofstream f(path);
        f << "feedUrl=wss://legacy.example.com/feed\n"
          << "feedSymbols=SPY,QQQ\n";
    }

    util::Config cfg;
    EXPECT_TRUE(cfg.loadFromFile(path));

    // No feed.N.* keys present, so feeds vector should be empty.
    EXPECT_TRUE(cfg.feeds.empty());

    // Legacy fields should be populated.
    EXPECT_EQ(cfg.feedUrl, "wss://legacy.example.com/feed");
    EXPECT_EQ(cfg.feedSymbols, (std::vector<std::string>{"SPY", "QQQ"}));

    std::remove(path);
}

TEST(GapFixesTest, MultiFeedSingleEntry) {
    const char* path = "test_gap_single_feed.ini";
    {
        std::ofstream f(path);
        f << "feed.0.url=ws://localhost:8100/feed\n";
        // adapter and symbols not specified — should use defaults
    }

    util::Config cfg;
    EXPECT_TRUE(cfg.loadFromFile(path));

    ASSERT_EQ(cfg.feeds.size(), 1u);
    EXPECT_EQ(cfg.feeds[0].url, "ws://localhost:8100/feed");
    EXPECT_EQ(cfg.feeds[0].adapter, "itch");  // default
    EXPECT_EQ(cfg.feeds[0].symbols, (std::vector<std::string>{"*"}));  // default

    std::remove(path);
}
