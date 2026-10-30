// tests/support/ClockCoherentHarness.hpp — ENC-1562 (purity SPEC D13)
//
// The ENC-1335 drive harness, shared by tests/dispatch/ClockCoherentPullTest.cpp
// and tests/market/ClockCoherentCorpusSweepTest.cpp. Factored out, unchanged,
// so the corpus sweep can move without a second copy of it.
//
// Header-only and test-only: nothing under include/ or src/ may depend on it.
#pragma once

#include "krn/AtomicStore.hpp"
#include "krn/Dispatcher.hpp"
#include "krn/Event.hpp"
#include "krn/StreamValue.hpp"
#include "krn/TreeBuilder.hpp"
#include "krn/nodes/INode.hpp"
#include "krn/reference/ReferenceComputer.hpp"
#include "krn/rt/ThreadPool.hpp"

#include "CorpusPath.hpp"

#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>

#include <cmath>
#include <fstream>
#include <future>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace enc1335 {

using namespace krn;

// ─── Terminal ──────────────────────────────────────────────────────────────
class Sink final : public krn::INode {
public:
  void onValue(const krn::StreamValue& sv) override {
    std::lock_guard<std::mutex> lk(mx_);
    if (const double* d = std::get_if<double>(&sv.value)) vals_.push_back(*d);
    else                                                  nonNumeric_.push_back(sv.symbol);
  }
  void shutdown() noexcept override {}
  std::vector<double> values() const {
    std::lock_guard<std::mutex> lk(mx_);
    return vals_;
  }
  std::size_t nonNumeric() const {
    std::lock_guard<std::mutex> lk(mx_);
    return nonNumeric_.size();
  }
private:
  mutable std::mutex mx_;
  std::vector<double> vals_;
  std::vector<std::string> nonNumeric_;
};

// ─── Corpus access (same resolver every other corpus-backed suite uses) ────
inline rapidjson::Document& corpusDoc() {
  static rapidjson::Document doc = [] {
    rapidjson::Document d;
    std::ifstream ifs = krn::testsupport::openCorpusRequests();
    if (!ifs.is_open()) { d.SetNull(); return d; }
    rapidjson::IStreamWrapper isw(ifs);
    d.ParseStream(isw);
    return d;
  }();
  return doc;
}

// ─── Ticks ─────────────────────────────────────────────────────────────────
// `lastPrice` plus a bid/ask pair around it, and `volume`, so every corpus
// field the sweep may bind is actually carried. `ev.type` is left at its
// default, "tick" — the production event type MarketTickComputer registers for.
//
// `Source::Reference` instead sends the reference computer's own event type
// carrying only its input field.
using krn::synthetic::ReferenceComputer;
enum class Source { Corpus, Reference };

inline void tick(krn::Dispatcher& d, const std::string& symbol, double price,
          Source src = Source::Corpus) {
  auto payload = std::make_shared<rapidjson::Document>();
  payload->SetObject();
  auto& al = payload->GetAllocator();
  krn::Event ev;
  if (src == Source::Reference) {
    const std::string_view in = ReferenceComputer::kInputField;
    payload->AddMember(rapidjson::StringRef(in.data(), in.size()), price, al);
    ev.type = std::string(ReferenceComputer::kEventType);
  } else {
    payload->AddMember("lastPrice", price,         al);
    payload->AddMember("bid",       price - 0.01,  al);
    payload->AddMember("ask",       price + 0.01,  al);
    payload->AddMember("volume",    100.0 + price, al);
  }
  ev.symbol  = symbol;
  ev.payload = payload;
  d.onTick(ev);
}

inline std::string render(const std::vector<double>& v) {
  std::ostringstream os;
  os << "[" << v.size() << "]" << std::fixed << std::setprecision(4);
  for (std::size_t i = 0; i < v.size() && i < 32; ++i) os << " " << v[i];
  if (v.size() > 32) os << " …";
  return os.str();
}

// NaN-aware. The TA suite writes a NaN `sma_5` for the first few ticks (too
// little history), and that NaN is part of the per-tick truth: a fix that
// smoothed it away would be changing the answer, not fixing the timing.
inline bool sameValue(double a, double b) {
  if (std::isnan(a) && std::isnan(b)) return true;
  return a == b;
}
inline bool sameSequence(const std::vector<double>& a, const std::vector<double>& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) if (!sameValue(a[i], b[i])) return false;
  return true;
}

// ─── The harness ───────────────────────────────────────────────────────────
//
// One DAG, built by the real `tree::buildForRequest` from the verbatim corpus
// request, driven through the real `Dispatcher::onTick`. Nothing is stubbed,
// subclassed or reimplemented but the terminal, which `buildForRequest` takes
// as a parameter.
enum class Mode {
  DrainEachTick,   // the oracle: the pool is empty before the next tick
  LiveBurst,       // production's shape: ingress does not wait for the DAG
  BlockedPool,     // deterministic: every clock is queued before any runs
};

struct DriveResult {
  std::vector<double>              emitted;
  std::vector<std::optional<double>> storeAfterEachTick;  // the per-tick truth
  std::size_t                      nonNumeric{0};
  bool                             threw{false};
  std::string                      error;
};

// `probe` names an (symbol, field) the caller wants read out of the store on
// the ingress thread immediately after each tick. That read is the independent
// oracle: it is what the store held when the tick's clock was produced, and it
// never passes through the DAG at all.
inline DriveResult drive(const rapidjson::Value& request,
          const std::vector<std::string>& symbols,
          std::size_t ticks,
          unsigned threads,
          Mode mode,
          std::pair<std::string, std::string> probe = {},
          Source src = Source::Corpus) {
  DriveResult out;
  AtomicStore store;
  auto pool = std::make_shared<rt::ThreadPool>(threads);
  auto prevGlobalPool = gThreadPool;
  gThreadPool = pool;

  Dispatcher dispatcher(pool.get(), &store);
  if (src == Source::Reference)
    dispatcher.addComputer(std::make_unique<ReferenceComputer>());
  tree::Deps deps;
  deps.store      = &store;
  deps.pool       = pool.get();
  deps.dispatcher = &dispatcher;

  auto sink = std::make_shared<Sink>();
  tree::BuiltChain chain;
  try {
    chain = tree::buildForRequest(request, deps, sink);
  } catch (const std::exception& ex) {
    out.threw = true;
    out.error = ex.what();
    pool->shutdown();
    gThreadPool = prevGlobalPool;
    return out;
  }

  // BlockedPool: occupy every worker until the barrier is released, so no
  // delivery can run while ticks are being injected.
  std::promise<void> release;
  std::shared_future<void> released(release.get_future());
  if (mode == Mode::BlockedPool) {
    for (unsigned i = 0; i < threads; ++i)
      pool->post([released]() { released.wait(); });
  }

  for (std::size_t n = 0; n < ticks; ++n) {
    for (const auto& s : symbols) tick(dispatcher, s, 100.0 + double(n), src);
    if (!probe.first.empty()) {
      auto v = store.get(probe.first, probe.second);
      if (v.has_value()) {
        if (const double* d = std::get_if<double>(&v.value())) out.storeAfterEachTick.emplace_back(*d);
        else                                                   out.storeAfterEachTick.emplace_back(std::nullopt);
      } else {
        out.storeAfterEachTick.emplace_back(std::nullopt);
      }
    }
    if (mode == Mode::DrainEachTick) pool->drain();
  }

  if (mode == Mode::BlockedPool) release.set_value();
  pool->drain();

  out.emitted    = sink->values();
  out.nonNumeric = sink->nonNumeric();

  for (auto& n : chain.keepAlive) if (n) n->shutdown();
  if (chain.head) chain.head->shutdown();
  chain.keepAlive.clear();
  chain.head.reset();
  pool->shutdown();
  gThreadPool = prevGlobalPool;
  return out;
}

// The truth as a plain sequence: the store's value at each tick, with the ticks
// that had no value at all omitted — those must produce no arrival.
inline std::vector<double> truthSequence(const DriveResult& r) {
  std::vector<double> out;
  for (const auto& v : r.storeAfterEachTick) if (v.has_value()) out.push_back(*v);
  return out;
}

} // namespace enc1335
