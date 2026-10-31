// ENC-803 (M17): genuine concurrency / contention tests.
//
// The pre-existing StressTest.cpp writes only DISJOINT (symbol,field) keys —
// fully serialized, zero contention — and AtomicStoreTest's reader/writer test
// ends in SUCCEED() asserting nothing. With the store's locks deleted, both
// still pass. These tests create REAL write-write contention on the SAME key
// from many threads and continuous multi-reader / multi-writer overlap, with
// DETERMINISTIC assertions (value-domain membership, not timing). They are the
// tests that would surface a missing lock under ThreadSanitizer.

#include "krn/AtomicStore.hpp"
#include "krn/Dispatcher.hpp"
#include "krn/Event.hpp"
#include "krn/StreamValue.hpp"
#include "krn/nodes/INode.hpp"
#include "krn/rt/ThreadPool.hpp"
#include "krn/util/Config.hpp"

#include <gtest/gtest.h>
#include <rapidjson/document.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace krn;

// ---------------------------------------------------------------------------
// Write-write contention on ONE (symbol,field). N threads hammer the same key,
// each thread writing only values drawn from its OWN disjoint band so the final
// survivor identifies exactly one writer. The store must never end up with a
// torn / out-of-domain value; the final value must be the tail of SOME writer.
// ---------------------------------------------------------------------------
TEST(ConcurrencyContentionTest, SameKeyWriteWriteSurvivorIsValid) {
  AtomicStore store;
  constexpr int kThreads = 8;
  constexpr int kWrites  = 5000;

  std::vector<std::thread> ths;
  for (int t = 0; t < kThreads; ++t) {
    ths.emplace_back([&store, t]() {
      const int base = t * 1'000'000;
      for (int i = 0; i < kWrites; ++i) {
        store.set("HOT", "px", base + i);
      }
    });
  }
  for (auto& th : ths) th.join();

  auto v = store.get("HOT", "px");
  ASSERT_TRUE(v.has_value());
  int final = std::get<int>(*v);
  // Must be the last value of exactly one writer band: base..base+kWrites-1.
  int band = final / 1'000'000;
  int off  = final % 1'000'000;
  EXPECT_GE(band, 0);
  EXPECT_LT(band, kThreads);
  EXPECT_GE(off, 0);
  EXPECT_LT(off, kWrites) << "torn/garbage value observed: " << final;
}

// ---------------------------------------------------------------------------
// Multi-reader / multi-writer overlap on the SAME key. Writers store only
// values from the closed set [0, kDomain). Readers spin reading the same key
// and flag if they EVER observe a value outside that set (a torn read) or a
// wrong variant alternative. No timing assumptions — the readers loop until the
// writers are done, and the writers are not done until every reader has
// completed kMinReadsPerReader reads (ENC-1588).
// ---------------------------------------------------------------------------
TEST(ConcurrencyContentionTest, MultiReaderMultiWriterNoTornReads) {
  AtomicStore store;
  constexpr int kWriters = 4;
  constexpr int kReaders = 4;
  constexpr int kDomain  = 256;
  constexpr int kIters   = 20000;
  // ENC-1340's floor was 10,000 reads in total; ENC-1588 makes it per reader.
  constexpr long long kMinReadsPerReader = 2500;
  // Not a pass condition. It only turns "a reader never runs" into a failure
  // instead of a hang.
  constexpr auto kHangCap = std::chrono::seconds(60);

  // Seed so readers always find a value.
  store.set("RW", "v", 0);

  struct alignas(64) ReaderCount { std::atomic<long long> n{0}; };
  std::array<ReaderCount, kReaders> readerReads;
  auto minReaderReads = [&readerReads]() {
    long long m = readerReads[0].n.load(std::memory_order_relaxed);
    for (const auto& c : readerReads) m = std::min(m, c.n.load(std::memory_order_relaxed));
    return m;
  };

  std::atomic<bool>      bad{false};
  std::atomic<bool>      hitHangCap{false};
  std::atomic<int>       writersLeft{kWriters};
  std::atomic<long long> writes{0};
  const auto hangDeadline = std::chrono::steady_clock::now() + kHangCap;

  std::vector<std::thread> threads;
  for (int w = 0; w < kWriters; ++w) {
    threads.emplace_back([&]() {
      long long i = 0;
      // At least kIters writes, then keep writing until every reader has done
      // its minimum — so the readers' minimum is reached WHILE writes are
      // still happening, however the scheduler treats this box.
      for (;; ++i) {
        if (i >= kIters && minReaderReads() >= kMinReadsPerReader) break;
        if (i >= kIters && (i % 1024) == 0 &&
            std::chrono::steady_clock::now() > hangDeadline) {
          hitHangCap.store(true);
          break;
        }
        store.set("RW", "v", static_cast<int>(i % kDomain));
      }
      writes.fetch_add(i, std::memory_order_relaxed);
      --writersLeft;
    });
  }
  for (int r = 0; r < kReaders; ++r) {
    threads.emplace_back([&store, &bad, &writersLeft, &readerReads, r]() {
      long long local = 0;
      if (local == 0) return; /*MUT-B*/
      while (writersLeft.load() > 0) {
        auto v = store.get("RW", "v");
        readerReads[r].n.store(++local, std::memory_order_relaxed);
        if (!v) continue;
        try {
          int x = std::get<int>(*v);
          if (x < 0 || x >= kDomain) bad.store(true);
        } catch (...) {
          bad.store(true); // wrong variant alternative => torn write
        }
      }
    });
  }
  for (auto& th : threads) th.join();

  EXPECT_FALSE(bad.load())
      << "reader observed an out-of-domain or torn value — AtomicStore locking "
         "did not hold under multi-reader/multi-writer contention";

  // ── ENC-1340 / ENC-1588: ANTI-VACUITY ─────────────────────────────────────
  // "No torn read was observed" is only evidence if reads were observed at
  // all, and observed while the writers were writing. ENC-1340 asserted a
  // floor of 10,000 reads over a run whose length was set by the writers, so
  // under machine load the readers could be descheduled for most of it and
  // the test failed on CPU starvation with no torn read (ENC-1588: 3/15
  // passes on an unchanged base). The run is now bounded by reader progress
  // instead: the writers keep writing until every reader has done
  // kMinReadsPerReader reads, so the bound holds on any box that schedules
  // the readers at all, and a reader that never reads fails here at kHangCap.
  //
  // What this no longer catches: a reader back-off in AtomicStore that makes
  // the readers slow (ENC-1340's 64-yield first cut fell to 2,335 reads in
  // 14 ms). That now lengthens this test instead of failing it; the yield
  // bound is pinned by the table in src/core/AtomicStore.cpp.
  long long total = 0;
  for (const auto& c : readerReads) total += c.n.load();
  RecordProperty("reads", std::to_string(total));
  RecordProperty("writes", std::to_string(writes.load()));
  EXPECT_FALSE(hitHangCap.load())
      << "the writers gave up after " << kHangCap.count()
      << " s waiting for the readers to reach " << kMinReadsPerReader
      << " reads each";
  EXPECT_GE(minReaderReads(), kMinReadsPerReader)
      << "a reader barely ran (" << minReaderReads() << " reads; " << total
      << " in total against " << writes.load()
      << " writes) — this test asserts the absence of torn reads over a sample "
         "that is effectively empty, so it proves nothing. Do not relax this "
         "bound; find out why the readers are not overlapping the writers.";
}

// ---------------------------------------------------------------------------
// setBatch under concurrent same-symbol writers + readers. Each batch writes a
// coupled pair (a, a*2) for a in [0,kDomain). A reader that observes both keys
// could see two different batches (no cross-key atomicity is promised), but
// each INDIVIDUAL value must be in-domain — i.e. no torn single field.
// ---------------------------------------------------------------------------
TEST(ConcurrencyContentionTest, SetBatchConcurrentSameSymbolInDomain) {
  AtomicStore store;
  constexpr int kWriters = 4;
  constexpr int kDomain  = 512;
  constexpr int kIters   = 10000;

  store.setBatch("B", {{"a", ArgType{0.0}}, {"b", ArgType{0.0}}});

  std::atomic<bool> bad{false};
  std::atomic<int>  writersLeft{kWriters};

  std::vector<std::thread> threads;
  for (int w = 0; w < kWriters; ++w) {
    threads.emplace_back([&store, &writersLeft]() {
      for (int i = 0; i < kIters; ++i) {
        double a = static_cast<double>(i % kDomain);
        store.setBatch("B", {{"a", ArgType{a}}, {"b", ArgType{a * 2.0}}});
      }
      --writersLeft;
    });
  }
  threads.emplace_back([&store, &bad, &writersLeft]() {
    while (writersLeft.load() > 0) {
      auto a = store.get("B", "a");
      auto b = store.get("B", "b");
      try {
        if (a) { double x = std::get<double>(*a); if (x < 0 || x >= kDomain) bad.store(true); }
        if (b) { double y = std::get<double>(*b); if (y < 0 || y >= 2 * kDomain) bad.store(true); }
      } catch (...) { bad.store(true); }
    }
  });
  for (auto& th : threads) th.join();

  EXPECT_FALSE(bad.load()) << "torn setBatch field observed under contention";
}

// ---------------------------------------------------------------------------
// Dispatcher concurrency via its public API: many threads call onTick() on ONE
// shared Dispatcher for the same (symbol,field) while a listener is registered.
// This drives the per-field history, the listener fan-out, and the per-type
// computer cache concurrently. The deterministic contract asserted here is
// only "no crash + the listener saw at least the expected count of in-domain
// values" — enough to be meaningful, and a missing lock surfaces under TSan.
// ---------------------------------------------------------------------------
namespace {
class CountingNode : public INode {
public:
  std::atomic<int> count{0};
  std::atomic<bool> bad{false};
  void onValue(const StreamValue& sv) override {
    try {
      double v = std::get<double>(sv.value);
      // Values are drawn from [0, 1000); anything else is corruption.
      if (v < 0.0 || v >= 1000.0) bad.store(true);
    } catch (...) {
      bad.store(true);
    }
    ++count;
  }
  void shutdown() noexcept override {}
};

Event makeRawTick(const std::string& sym, const char* field, double v) {
  auto doc = std::make_shared<rapidjson::Document>();
  doc->SetObject();
  doc->AddMember(rapidjson::Value(field, doc->GetAllocator()),
                 rapidjson::Value(v), doc->GetAllocator());
  Event e;
  e.type = "tick";
  e.symbol = sym;
  e.payload = std::move(doc);
  return e;
}
} // namespace

TEST(ConcurrencyContentionTest, DispatcherConcurrentOnTickSameKey) {
  rt::ThreadPool pool(4);
  AtomicStore store;
  util::Config cfg;
  Dispatcher dispatcher(&pool, &store, cfg);

  auto node = std::make_shared<CountingNode>();
  // Bind on a raw, non-price field ("q") so the value is fanned out verbatim
  // by the dispatcher's direct-field path (independent of any TA computer).
  dispatcher.registerListener("CC", "q", node);

  constexpr int kThreads = 6;
  constexpr int kTicks   = 2000;
  std::vector<std::thread> ths;
  for (int t = 0; t < kThreads; ++t) {
    ths.emplace_back([&dispatcher, t]() {
      for (int i = 0; i < kTicks; ++i) {
        dispatcher.onTick(makeRawTick("CC", "q", static_cast<double>((t * 100 + i) % 1000)));
      }
    });
  }
  for (auto& th : ths) th.join();
  pool.drain();
  pool.shutdown();

  EXPECT_FALSE(node->bad.load()) << "listener observed a corrupt value";
  EXPECT_GE(node->count.load(), 1) << "no values delivered under concurrency";
}
