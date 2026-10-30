// AtomicKeyNamespaceTest's market case, lifted from
// tests/dispatch/AtomicKeyNamespaceTest.cpp by ENC-1562 (purity SPEC D13): it
// needs MarketTickComputer's `mean`, so it runs under krn_market_tests' market
// bootstrap. The helpers are the subset of that file's that this case uses.

#include "krn/AtomicStore.hpp"
#include "krn/Dispatcher.hpp"
#include "krn/Event.hpp"
#include "krn/StreamValue.hpp"
#include "krn/nodes/AtomicAccessor.hpp"
#include "krn/nodes/INode.hpp"
#include "krn/rt/ThreadPool.hpp"
#include "krn/util/Config.hpp"

#include <gtest/gtest.h>
#include <rapidjson/document.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using namespace krn;

namespace {

class Recorder : public INode {
public:
  void onValue(const StreamValue& sv) override {
    std::lock_guard<std::mutex> lk(mx_);
    received_.push_back(sv);
  }
  void shutdown() noexcept override {}

  // Sorted, because IntegrationTest-style ordering across a thread pool is not
  // guaranteed (ENC-1085) — these arrive via ThreadPool::post.
  std::vector<double> sortedValues() const {
    std::lock_guard<std::mutex> lk(mx_);
    std::vector<double> out;
    for (const auto& sv : received_) {
      if (std::holds_alternative<double>(sv.value)) {
        out.push_back(std::get<double>(sv.value));
      } else if (std::holds_alternative<int>(sv.value)) {
        out.push_back(static_cast<double>(std::get<int>(sv.value)));
      }
    }
    std::sort(out.begin(), out.end());
    return out;
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lk(mx_);
    return received_.size();
  }

private:
  mutable std::mutex mx_;
  std::vector<StreamValue> received_;
};

Event makeTick(const std::string& symbol,
               const std::vector<std::pair<std::string, double>>& fields) {
  auto doc = std::make_shared<rapidjson::Document>();
  doc->SetObject();
  auto& alloc = doc->GetAllocator();
  for (const auto& [name, value] : fields) {
    doc->AddMember(rapidjson::Value(name.c_str(), alloc),
                   rapidjson::Value(value), alloc);
  }
  return Event{symbol, std::move(doc)};
}

double asDouble(const ArgType& v) {
  if (std::holds_alternative<double>(v)) return std::get<double>(v);
  if (std::holds_alternative<int>(v)) return static_cast<double>(std::get<int>(v));
  ADD_FAILURE() << "stored value is neither double nor int";
  return 0.0;
}

util::Config cfgWith(bool namespaced) {
  util::Config c;
  c.atomicKeyNamespaceByField = namespaced;
  return c;
}

}  // namespace

// --- The subtler half of the break: mean / median / spread. ---------------
//
// `mean`, `median` and `spread` are registered FunctionMap builtins AND keys
// that MarketTickComputer writes itself (MarketTA.cpp ~91-92 and ~391), over
// PRICE history rather than over the listened field's history. Today the
// computers run before the listener fan-out in onTick, so the Dispatcher's
// builtin is the last writer and wins the bare key.
//
// With namespacing on the Dispatcher vacates that key, and MarketTA's value —
// a different number, not a missing one — is what an AtomicAccessor on bare
// `mean` reads. So for these three names the migration is a SILENT VALUE
// CHANGE, which is strictly worse to debug than a key that stops resolving,
// and it is the sharpest reason the flag defaults off.
//
// It also splits push from pull: a Listener on bare `mean` still receives the
// Dispatcher's per-field mean while an AtomicAccessor on bare `mean` now reads
// MarketTA's price mean. Under the default they agree.
TEST(AtomicKeyNamespaceTest, MarketTAKeepsTheBareKeyAliveWithADifferentValue) {
  // Listen on `volume`, not on the price field, so the two producers' means
  // are different numbers by construction:
  //   MarketTA   mean over prices  [1, 3, 2]    = 2
  //   Dispatcher mean over volumes [10, 20, 30] = 20
  const auto drive = [](Dispatcher& md) {
    md.registerListener("MKT.SYM", "volume", std::make_shared<Recorder>());
    md.registerListener("MKT.SYM", "mean", std::make_shared<Recorder>());
    const double prices[]  = {1.0, 3.0, 2.0};
    const double volumes[] = {10.0, 20.0, 30.0};
    for (int i = 0; i < 3; ++i) {
      md.onTick(makeTick("MKT.SYM", {{"lastPrice", prices[i]},
                                     {"volume", volumes[i]}}));
    }
  };

  {
    rt::ThreadPool pool(1);
    AtomicStore store;
    Dispatcher md(&pool, &store, cfgWith(false));
    drive(md);
    pool.shutdown();

    auto bare = store.get("MKT.SYM", "mean");
    ASSERT_TRUE(bare.has_value());
    EXPECT_DOUBLE_EQ(asDouble(*bare), 20.0)
        << "with the flag off the Dispatcher's builtin must remain the last "
           "writer of the bare key, ahead of MarketTA's price mean";
  }
  {
    rt::ThreadPool pool(1);
    AtomicStore store;
    Dispatcher md(&pool, &store, cfgWith(true));
    drive(md);
    pool.shutdown();

    auto bare = store.get("MKT.SYM", "mean");
    ASSERT_TRUE(bare.has_value())
        << "the bare key does NOT go away here — MarketTA still writes it";
    EXPECT_DOUBLE_EQ(asDouble(*bare), 2.0)
        << "with the flag on, bare `mean` must read MarketTA's price mean; "
           "this is a silent value change for anything bound to it";

    auto ns = store.get("MKT.SYM", "volume.mean");
    ASSERT_TRUE(ns.has_value());
    EXPECT_DOUBLE_EQ(asDouble(*ns), 20.0)
        << "the Dispatcher's per-field mean must be reachable under its "
           "namespaced key";
  }
}
