// ENC-1486 — the engine half of the record ingress tests (split by ENC-1557).
//
// Design: specs/2026-10-14-krn-data-layer/SPEC.md D1, D3, D9;
//         specs/2026-10-15-gma-engine-purity/SPEC.md D4, D13.
//
// `Dispatcher::onRecord` takes a `data::SymbolId` and an `engine::FieldSource`
// (ENC-1557). What it does with them is engine code and is tested here, in
// krn_tests: the record path names no JSON (section 3) and an unresolvable
// symbol is refused, once and out loud (sections 5-6). The parity of the two
// entry points over a real `TickRecord`, and the record-to-field mapping, need
// `RecordFieldSource` and live in krn_data_tests:
// krn-data's data/tests/RecordIngressParityTest.cpp.
//
// The refusal cases drive `FixedFields`, a FieldSource with no record behind
// it. That is the point: the refusal is decided on `symbolId` before a field
// is read, so it is the engine's whatever the source.
//
// NO PROCESS-GLOBAL REGISTRY IS WRITTEN BY THIS FILE (ENC-1102). The listeners
// are raw `INode`s registered directly on the rig's own Dispatcher.

#include "krn/AtomicStore.hpp"
#include "krn/Dispatcher.hpp"
#include "krn/StreamValue.hpp"
#include "krn/engine/SymbolTable.hpp"
#include "krn/engine/FieldSource.hpp"
#include "krn/nodes/INode.hpp"
#include "krn/rt/ThreadPool.hpp"
#include "krn/util/Config.hpp"
#include "krn/util/Logger.hpp"

#include <gtest/gtest.h>

#include <cctype>
#include <cstdio>
#include <fstream>
#include <unistd.h>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using namespace krn;

namespace {

constexpr char kTicker[] = "PARITY";

// One tick's fields, with no record behind them.
class FixedFields final : public engine::FieldSource {
public:
  explicit FixedFields(double price) : price_(price) {}
  bool isObject() const noexcept override { return true; }
  std::size_t fieldCountHint() const noexcept override { return 2; }
  bool has(const std::string& name) const override {
    return name == "price" || name == "size";
  }
  bool tryGetNumber(const std::string& name, double& out) const override {
    if (name == "price") { out = price_; return true; }
    if (name == "size")  { out = 10.0;   return true; }
    return false;
  }
  bool tryGetUint64(const std::string&, std::uint64_t&) const override { return false; }
  void forEachNumber(engine::NumericFieldSink& sink) const override {
    sink.field("price", price_);
    sink.field("size", 10.0);
  }
private:
  double price_;
};

class Recorder final : public INode {
public:
  explicit Recorder(std::vector<std::string>* rows, std::mutex* mx)
    : rows_(rows), mx_(mx) {}
  void onValue(const StreamValue& sv) override {
    std::lock_guard<std::mutex> lk(*mx_);
    rows_->push_back(sv.symbol);
  }
  void shutdown() noexcept override {}
private:
  std::vector<std::string>* rows_;
  std::mutex*               mx_;
};

// One Dispatcher, one store, one single-threaded pool; ends on `drain()`.
struct Rig {
  explicit Rig(const util::Config& cfg)
    : pool(1), disp(&pool, &store, cfg) {
    for (const char* field : {"price", "size"}) {
      auto node = std::make_shared<Recorder>(&rows, &mx);
      nodes.push_back(node);
      disp.registerListener(kTicker, field, node);
    }
    symbols.intern("ZZ-PAD");      // burn id 1 so kTicker is not id 1 by luck
    symbolId = symbols.intern(kTicker);
    disp.setSymbolTable(&symbols);
  }

  void feed(data::SymbolId id, double price) {
    const FixedFields fields(price);
    disp.onRecord(id, fields);
  }

  void quiesce() { pool.drain(); }

  struct Log {
    const Rig* rig;
    std::vector<std::string> rows() const {
      std::lock_guard<std::mutex> lk(rig->mx);
      return rig->rows;
    }
  };

  rt::ThreadPool                      pool;
  AtomicStore                         store;
  data::SymbolTable                   symbols;
  data::SymbolId                      symbolId = 0;
  Dispatcher                          disp;
  mutable std::mutex                  mx;
  std::vector<std::string>            rows;
  std::vector<std::shared_ptr<INode>> nodes;
  Log                                 log{this};
};

util::Config defaultCfg() {
  util::Config cfg;
  cfg.threadPoolSize = 1;   // krn-data's docs/data-layer.md §C7
  return cfg;
}

}  // namespace

// ===========================================================================
// 3. STRUCTURAL — the record path names no JSON type
// ===========================================================================
//
// "No rapidjson::Document is constructed on this path" cannot be asserted by
// running the code: a DOM built and discarded leaves no observable trace in a
// delivery log. So it is asserted over the SOURCE of the record path, with a
// positive control in the same file, read by the same reader, against the same
// token list — because a negative from a search that has not been seen to
// return a true positive is not a result (the workspace-root grep that reaches
// none of the seven repos is the same bug).
//
// Comments and string literals are stripped before the search. Prose about
// JSON is not a JSON dependency, and a token-counting gate that cannot tell
// the difference is one this workspace has already been bitten by.
//
// WHAT THIS GATE DOES NOT CATCH, stated because an unstated limit is how a
// gate gets trusted past its range: it is textual and per-file, so moving DOM
// construction into a THIRD translation unit that `onRecord` merely calls
// would evade it. What it does catch is the regression that actually threatens
// this path — someone "unifying" the two entry points by synthesizing an
// `Event` inside `onRecord` — and it catches the shared `ingest` body too,
// which is where such a change would otherwise hide. A link-time or
// symbol-level check would close the rest; it is not built, and this comment
// is the honest statement of that rather than a claim that it is.

namespace {

std::string readSource(const std::string& relative) {
  const std::string path = std::string(KRN_TEST_SOURCE_DIR) + "/" + relative;
  std::FILE* f = std::fopen(path.c_str(), "rb");
  // ENC-1233 §2 D10: a missing fixture is a failure, never a skip.
  if (!f) return {};
  std::string out;
  char buf[8192];
  std::size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
  std::fclose(f);
  return out;
}

// Replaces every comment and string/char literal with spaces, so what is left
// is only what the translation unit NAMES.
std::string scrubComments(const std::string& src) {
  std::string out(src.size(), ' ');
  enum class S { Code, Line, Block, Str, Chr } st = S::Code;
  for (std::size_t i = 0; i < src.size(); ++i) {
    const char c = src[i];
    const char n = (i + 1 < src.size()) ? src[i + 1] : '\0';
    switch (st) {
      case S::Code:
        if (c == '/' && n == '/') { st = S::Line; }
        else if (c == '/' && n == '*') { st = S::Block; }
        else if (c == '"') { st = S::Str; }
        else if (c == '\'') { st = S::Chr; }
        else out[i] = c;
        break;
      case S::Line:
        if (c == '\n') { st = S::Code; out[i] = c; }
        break;
      case S::Block:
        if (c == '*' && n == '/') { st = S::Code; ++i; }
        else if (c == '\n') out[i] = c;
        break;
      case S::Str:
        if (c == '\\') ++i;
        else if (c == '"') st = S::Code;
        break;
      case S::Chr:
        if (c == '\\') ++i;
        else if (c == '\'') st = S::Code;
        break;
    }
  }
  return out;
}

// The body of `Dispatcher::<name>` in already-scrubbed source, brace-matched.
// Empty when the function is not found, which every caller treats as a failure
// rather than as "nothing to check".
std::string functionBody(const std::string& scrubbed, const std::string& name) {
  const std::string sig = "Dispatcher::" + name + "(";
  const std::size_t at = scrubbed.find(sig);
  if (at == std::string::npos) return {};
  const std::size_t open = scrubbed.find('{', at);
  if (open == std::string::npos) return {};
  int depth = 0;
  for (std::size_t i = open; i < scrubbed.size(); ++i) {
    if (scrubbed[i] == '{') ++depth;
    else if (scrubbed[i] == '}') {
      if (--depth == 0) return scrubbed.substr(open, i - open + 1);
    }
  }
  return {};
}

// Every rapidjson-shaped name the record path could plausibly reach for, plus
// `Event` (the DOM-carrying ingress type) and `JsonFieldSource` (the adapter).
//
// THE LIST WAS TOO SHORT ON ITS FIRST RUN and an adversarial probe measured
// it: inserting `const Event& evAlias = …` into `onRecord` left all four gate
// tests green, because `Event` was not on it, and neither were `SetObject`,
// `AddMember`, `Parse`, `GetString`, `IsNumber` or `IsObject`. A synthesized
// DOM did go red, on `payload` — so the gate was effectively adequate and
// nominally sloppy, which is the state in which a gate stops being adequate
// without anyone noticing.
//
// Case matters and is load-bearing: rapidjson's `IsObject`/`IsNumber` are
// capital-I, while `FieldSource`'s own `isObject`/`tryGetNumber` are not, so
// the record path's legitimate calls do not trip these.
const std::vector<std::string>& jsonTokens() {
  static const std::vector<std::string> t = {
      "rapidjson", "Document", "payload", "Event",
      "HasMember", "MemberBegin", "MemberEnd", "FindMember", "AddMember",
      "SetObject", "GetAllocator", "Parse",
      "GetDouble", "GetUint64", "GetString", "GetStringLength",
      "IsNumber", "IsObject", "IsString", "IsUint64",
      "JsonFieldSource"};
  return t;
}

// Matches a token only as a WHOLE IDENTIFIER. Both bounds are load-bearing
// and each was added after a measured false positive, which is the argument
// for keeping them:
//
//   * leading bound — plain substring search made `GetUint64` match inside
//     `tryGetUint64`, the FieldSource accessor, so RecordFieldSource.hpp
//     "named rapidjson" on this gate's very first run;
//   * trailing bound — adding `Event` to the list (because an adversarial
//     probe slipped `const Event& evAlias` past the old list) made it match
//     inside `EventComputerRegistry`, which is ordinary engine code, so the
//     shared `ingest` body "named JSON".
//
// With both, `value.GetUint64()` and `const Event&` are hits while
// `tryGetUint64` and `EventComputerRegistry` are not.
bool identChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool wholeIdentifierAt(const std::string& hay, std::size_t at, std::size_t len) {
  if (at != 0 && identChar(hay[at - 1])) return false;
  const std::size_t after = at + len;
  if (after < hay.size() && identChar(hay[after])) return false;
  return true;
}

std::vector<std::string> jsonTokensIn(const std::string& scrubbed) {
  std::vector<std::string> found;
  for (const auto& tok : jsonTokens()) {
    for (std::size_t at = scrubbed.find(tok); at != std::string::npos;
         at = scrubbed.find(tok, at + 1)) {
      if (wholeIdentifierAt(scrubbed, at, tok.size())) {
        found.push_back(tok);
        break;
      }
    }
  }
  return found;
}

}  // namespace

TEST(RecordPathIsJsonFreeTest, PositiveControlTheJsonPathDoesNameJson) {
  const std::string src = readSource("src/core/Dispatcher.cpp");
  ASSERT_FALSE(src.empty()) << "src/core/Dispatcher.cpp not readable from "
                            << KRN_TEST_SOURCE_DIR;
  const std::string body = functionBody(scrubComments(src), "onTick");
  ASSERT_FALSE(body.empty()) << "could not locate Dispatcher::onTick — the "
                                "negative verdicts below would be vacuous";
  // If this ever goes green-by-emptiness, every other test in this suite is
  // measuring nothing.
  EXPECT_FALSE(jsonTokensIn(body).empty())
      << "the JSON ingress path names no JSON token — the scrubber or the "
         "token list is broken, not the code";
}

TEST(RecordPathIsJsonFreeTest, SharedIngestBodyNamesNoJson) {
  const std::string src = readSource("src/core/Dispatcher.cpp");
  ASSERT_FALSE(src.empty());
  const std::string body = functionBody(scrubComments(src), "ingest");
  ASSERT_FALSE(body.empty()) << "could not locate Dispatcher::ingest";
  ASSERT_GT(body.size(), 500u) << "Dispatcher::ingest body implausibly short — "
                                  "brace matching probably failed";
  const auto found = jsonTokensIn(body);
  EXPECT_TRUE(found.empty())
      << "Dispatcher::ingest is the body BOTH entry points share; it must be "
         "JSON-free. Found: " << (found.empty() ? std::string{} : found[0]);
}

TEST(RecordPathIsJsonFreeTest, RecordPathTranslationUnitNamesNoJson) {
  const std::string src = readSource("src/core/DispatcherRecord.cpp");
  ASSERT_FALSE(src.empty()) << "src/core/DispatcherRecord.cpp not readable";
  const std::string scrubbed = scrubComments(src);
  const std::string body = functionBody(scrubbed, "onRecord");
  ASSERT_FALSE(body.empty()) << "could not locate Dispatcher::onRecord";

  auto found = jsonTokensIn(body);
  EXPECT_TRUE(found.empty())
      << "Dispatcher::onRecord names a JSON type: "
      << (found.empty() ? std::string{} : found[0]);

  // The whole TU, not just the one function: a helper beside it would be on
  // the same path.
  found = jsonTokensIn(scrubbed);
  EXPECT_TRUE(found.empty())
      << "src/core/DispatcherRecord.cpp names a JSON type: "
      << (found.empty() ? std::string{} : found[0]);
}

// ===========================================================================
// 5. REFUSAL — a record with no resolvable stream key goes nowhere
// ===========================================================================
//
// `onTick` has always dropped an event with an empty stream key. A record
// names a `symbolId` and nothing else, so the record path reaches the same
// rule from the other side, and there are three ways to arrive at it: no
// `SymbolTable` installed at all, `kInvalidSymbolId`, and an id the table
// never issued. All three must be NOTHING — not a delivery under "", which
// would collapse every symbol into one bucket and make every subscription
// miss. A silent wrong answer is worse than a loud nothing.
//
// Pinned because it is otherwise invisible: perturbing the refusal away left
// every parity and mapping assertion green.

TEST(RecordIngressRefusalTest, UnresolvableSymbolDeliversNothing) {
  const auto cfg = defaultCfg();

  const auto drove = [&cfg](bool installTable, data::SymbolId id) {
    Rig rig(cfg);
    if (!installTable) rig.disp.setSymbolTable(nullptr);
    rig.feed(id, 100.25);
    rig.quiesce();
    // BOTH halves matter. "no delivery to PARITY" alone is satisfied by a
    // path that routes everything under the EMPTY stream key — nothing is
    // subscribed there, so the listener log stays empty while the AtomicStore
    // quietly accumulates a `""` symbol. That is the actual failure mode of
    // dropping the refusal, and perturbing the refusal away left a check on
    // the first half alone completely green.
    const bool leaked = rig.store.get(kTicker, "price").has_value() ||
                        rig.store.get("", "price").has_value() ||
                        rig.store.get("", "size").has_value();
    return std::make_pair(rig.log.rows().size(), leaked);
  };

  // POSITIVE CONTROL FIRST: the rig does deliver for a resolvable id, so the
  // three zeros below are refusals and not a broken rig.
  {
    Rig rig(cfg);
    rig.feed(rig.symbolId, 100.25);
    rig.quiesce();
    ASSERT_GT(rig.log.rows().size(), 0u) << "the rig cannot deliver at all";
    ASSERT_TRUE(rig.store.get(kTicker, "price").has_value());
  }

  // No table installed.
  {
    auto [deliveries, leaked] = drove(/*installTable=*/false, 2);
    EXPECT_EQ(deliveries, 0u);
    EXPECT_FALSE(leaked);
  }
  // kInvalidSymbolId — "this record names no symbol".
  {
    auto [deliveries, leaked] = drove(true, data::kInvalidSymbolId);
    EXPECT_EQ(deliveries, 0u);
    EXPECT_FALSE(leaked);
  }
  // An id this table never issued.
  {
    auto [deliveries, leaked] = drove(true, 99'999);
    EXPECT_EQ(deliveries, 0u);
    EXPECT_FALSE(leaked);
  }
}

// ===========================================================================
// 6. THE REFUSAL LOGS ONCE, NOT ONCE PER RECORD
// ===========================================================================
//
// WHY THIS TEST EXISTS AT ALL, stated plainly because the history is the
// argument: the once-guard was reported as landed while only HALF of it was.
// `Dispatcher.hpp` declared `_warnedNoSymbolTable` / `_warnedUnresolvedSymbol`
// and `DispatcherRecord.cpp` used neither, so the members were DEAD and both
// refusals still logged per record. A `git checkout --` in a perturbation
// script reverted the half that lived in the .cpp, and nothing noticed, because
// nothing could: no test observed the log.
//
// Dead flags are worse than no attempt — the next reader greps the header,
// finds `_warnedNoSymbolTable`, and concludes it is handled. So the guard now
// has an instrument, and the instrument reads the actual log output rather
// than the presence of a member.
//
// WHAT IS BEING PROTECTED IS A MEASUREMENT. `onRecord` is step 2 of ENC-1487's
// five-step B2, the number SPEC D13 sizes the project by, and it carries MBO at
// tens of millions of records per symbol-day. `util::Logger` has no throttling
// API, so an unguarded `Warn` here makes B2 measure logging.

namespace {

// Redirects the PROCESS-GLOBAL logger to a private file for the duration of a
// test, then puts it back.
//
// Three deliberate choices:
//   * The ticket id is in the path and the suffix comes from `mkstemp`, because
//     parallel agents share one /tmp and a bare name gets clobbered.
//   * It restores the LEVEL it found (the `QuietLogger` pattern in
//     tests/ws/ClientSessionBackpressureTest.cpp), and restores the
//     DESTINATION to stdout. `Logger` exposes no getter for its current file,
//     so "" is the only restorable value — which is correct here precisely
//     because nothing in src/ or tests/ ever points the singleton at a file
//     (verified: the only singleton mutation in the tree is that QuietLogger's
//     setLevel). If that ever stops being true, this guard needs a real
//     save/restore and this comment is the warning.
//   * It is the global logger and not a local `Logger`, because the Dispatcher
//     logs through `krn::util::logger()` and a test against a local instance
//     would prove nothing about the code under test.
class GlobalLogCapture {
public:
  GlobalLogCapture() : savedLevel_(krn::util::logger().level()) {
    char tmpl[] = "/tmp/ENC-1486-logcap-XXXXXX";
    const int fd = ::mkstemp(tmpl);
    path_ = tmpl;
    if (fd >= 0) ::close(fd);
    krn::util::logger().setLevel(krn::util::LogLevel::Warn);
    krn::util::logger().setFile(path_);
  }

  ~GlobalLogCapture() {
    krn::util::logger().setFile("");          // flush + close, back to stdout
    krn::util::logger().setLevel(savedLevel_);
    std::remove(path_.c_str());
  }

  // Flushes by closing, so call this once, at the end of a drive.
  std::string contents() const {
    krn::util::logger().setFile("");
    std::ifstream f(path_);
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
  }

  static std::size_t count(const std::string& hay, const std::string& needle) {
    std::size_t n = 0;
    for (std::size_t at = hay.find(needle); at != std::string::npos;
         at = hay.find(needle, at + needle.size())) {
      ++n;
    }
    return n;
  }

private:
  krn::util::LogLevel savedLevel_;
  std::string         path_;
};

// Big enough that "once" and "once per record" cannot be confused, small
// enough to stay instant.
constexpr int kRefusalDrives = 50;

}  // namespace

TEST(RecordIngressRefusalTest, MissingSymbolTableIsLoggedOnceNotPerRecord) {
  const auto cfg = defaultCfg();
  std::string log;
  {
    GlobalLogCapture capture;
    Rig rig(cfg);
    rig.disp.setSymbolTable(nullptr);
    for (int i = 0; i < kRefusalDrives; ++i) {
      rig.feed(rig.symbolId, 100.0 + i);
    }
    rig.quiesce();
    log = capture.contents();
  }

  const std::size_t n =
      GlobalLogCapture::count(log, "onRecord with no SymbolTable installed");

  // ONE assertion, self-controlling in BOTH directions, which is why it is
  // `EQ(1)` and not `LT(kRefusalDrives)`:
  //   n == 0            -> the capture never saw the log; the test is broken,
  //                        not passing.
  //   n == 50           -> the guard is missing (the state this branch shipped
  //                        in once already).
  //   1 < n < 50        -> the guard is racy.
  EXPECT_EQ(n, 1u)
      << "drove " << kRefusalDrives << " records with no SymbolTable and the "
         "refusal logged " << n << " times; expected exactly 1. 0 means the "
         "log capture is broken (so every other assertion here is vacuous); "
         << kRefusalDrives << " means the once-guard is gone and ENC-1487's "
         "step-2 measurement would be measuring Logger.";
}

TEST(RecordIngressRefusalTest, UnresolvableSymbolIsLoggedOnceNotPerRecord) {
  const auto cfg = defaultCfg();
  std::string log;
  {
    GlobalLogCapture capture;
    Rig rig(cfg);
    for (int i = 0; i < kRefusalDrives; ++i) {
      // An id the table never issued — route 2 of the three.
      rig.feed(99'999, 100.0 + i);
    }
    rig.quiesce();
    log = capture.contents();
  }

  const std::size_t n = GlobalLogCapture::count(
      log, "onRecord symbolId does not resolve to a ticker");
  EXPECT_EQ(n, 1u)
      << "drove " << kRefusalDrives << " unresolvable records and the refusal "
         "logged " << n << " times; expected exactly 1 (0 = broken capture, "
      << kRefusalDrives << " = missing guard)";
}

TEST(RecordIngressRefusalTest, AnEmptyTickerIsRefusedAndSaidOutLoud) {
  // ROUTE 3, which previously fell through with NO log at all:
  // `SymbolTable::intern("")` issues a real, contains()-true id whose `name()`
  // is "". The ruling is that this is a refusal — see the block comment in
  // src/core/DispatcherRecord.cpp for why admitting "" is a silent wrong
  // answer rather than a degraded one.
  const auto cfg = defaultCfg();
  std::string log;
  std::size_t deliveries = 0;
  bool leaked = false;

  {
    GlobalLogCapture capture;
    Rig rig(cfg);

    // The id is REAL — this is the part that makes route 3 distinct from the
    // other two, so assert it rather than assuming it.
    const auto emptyId = rig.symbols.intern("");
    ASSERT_NE(emptyId, data::kInvalidSymbolId)
        << "SymbolTable folded intern(\"\") onto kInvalidSymbolId; route 3 no "
           "longer exists and this test should be deleted, not relaxed";
    ASSERT_TRUE(rig.symbols.contains(emptyId));
    ASSERT_TRUE(rig.symbols.name(emptyId).empty());

    for (int i = 0; i < kRefusalDrives; ++i) {
      rig.feed(emptyId, 100.0 + i);
    }
    rig.quiesce();

    deliveries = rig.log.rows().size();
    leaked = rig.store.get("", "price").has_value() ||
             rig.store.get(kTicker, "price").has_value();
    log = capture.contents();
  }

  EXPECT_EQ(deliveries, 0u) << "an empty ticker delivered to a listener";
  EXPECT_FALSE(leaked) << "an empty ticker reached the AtomicStore";
  EXPECT_EQ(GlobalLogCapture::count(
                log, "onRecord symbolId does not resolve to a ticker"),
            1u)
      << "route 3 must be refused ONCE and out loud — it used to be silent";
}
