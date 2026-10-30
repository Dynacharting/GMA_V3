// ENC-1335 test 6, moved here from tests/dispatch/ClockCoherentPullTest.cpp by
// ENC-1562 (purity SPEC D13): it sweeps the market corpus under the market
// computer, so it runs in krn_market_tests. Tests 1-5 stay in krn_tests.

#include "../support/ClockCoherentHarness.hpp"

#include <gtest/gtest.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace enc1335 {

std::vector<std::string> requestStreamKeys(const rapidjson::Value& v,
                                           int depth = 0) {
  std::vector<std::string> out;
  if (depth > 24) return out;
  if (v.IsObject()) {
    for (auto it = v.MemberBegin(); it != v.MemberEnd(); ++it) {
      if (std::string(it->name.GetString()) == "streamKey" && it->value.IsString())
        out.emplace_back(it->value.GetString());
      auto sub = requestStreamKeys(it->value, depth + 1);
      out.insert(out.end(), sub.begin(), sub.end());
    }
  } else if (v.IsArray()) {
    for (auto& c : v.GetArray()) {
      auto sub = requestStreamKeys(c, depth + 1);
      out.insert(out.end(), sub.begin(), sub.end());
    }
  }
  return out;
}

} // namespace enc1335

// ═══ 6. THE SAME COMPARISON, ACROSS ALL 272 ════════════════════════════════
//
// The per-entry claim driven over the whole corpus: for every entry that emits
// anything, the live-ingress sequence must equal the drained-per-tick one.
//
// Entries whose DAG is timer-driven (`Interval`, `BucketTime`, 24 of them) are
// EXCLUDED and counted, not silently passed: their clock is a wall-clock timer,
// so two runs legitimately differ in how many pulses land inside the test and
// there is no tick for the sample to be coherent WITH. That is the residual
// this ticket does not fix, named with a number rather than left implicit.
TEST(ClockCoherentPull, NoCorpusEntryEmitsADifferentSequenceUnderLiveIngress) {
  using namespace enc1335;
  rapidjson::Document& doc = corpusDoc();
  ASSERT_FALSE(doc.IsNull()) << krn::testsupport::corpusNotFoundDiagnostic();
  ASSERT_TRUE(doc.IsArray());

  constexpr std::size_t kSweepTicks = 30;
  std::size_t checked = 0, emitted = 0, timerDriven = 0, built = 0, refused = 0;
  // ANTI-VACUITY. An entry whose drained sequence is CONSTANT cannot tell two
  // sampling rules apart: a mistimed sample of an unchanging value is the same
  // value. `differing == 0` is therefore only meaningful alongside a floor on
  // how many entries could have differed at all — 159 of 248 at the budget
  // below. Without this counter, shortening the window until every sequence
  // went flat would turn this test green while changing nothing.
  //
  // THE BUDGET IS WHY THE MEASURED VERDICT CHANGE UNDERSTATES THE DEFECT.
  // 88 of the 272 entries structurally bind an `AtomicAccessor` at a clock hop
  // (81 + 7, see EveryCorpusAtomicAccessorSitsAtAClockHop). Under the mutation
  // that disables the fix, 56-60 of the 248 driven here differ over 6 runs —
  // not 88 — because 30 ticks is not enough history for the long-period fields
  // (`sma_50`, `ema_50`, `rsi_14`, …) to reach the store at all, so those
  // entries emit nothing in BOTH arms. At 8 ticks the same mutation differed in
  // only 33-36. The number is a floor set by the window, not a count of the
  // entries at risk.
  std::size_t sensitive = 0;
  std::vector<std::string> differing;

  for (auto& e : doc.GetArray()) {
    if (!e.IsObject() || !e.HasMember("request")) continue;
    const int id = (e.HasMember("corpus_id") && e["corpus_id"].IsInt())
                     ? e["corpus_id"].GetInt() : -1;
    const auto& r = e["request"];

    // Timer-driven: excluded and counted.
    {
      rapidjson::StringBuffer sb;
      rapidjson::Writer<rapidjson::StringBuffer> w(sb);
      r.Accept(w);
      const std::string json(sb.GetString(), sb.GetSize());
      if (json.find("\"Interval\"") != std::string::npos ||
          json.find("\"BucketTime\"") != std::string::npos ||
          json.find("\"TumblingWindow\"") != std::string::npos) {
        ++timerDriven;
        continue;
      }
    }

    const auto syms = requestStreamKeys(r);
    if (syms.empty()) continue;

    DriveResult a = drive(r, syms, kSweepTicks, 1, Mode::DrainEachTick);
    if (a.threw) { ++refused; continue; }
    ++built;
    DriveResult b = drive(r, syms, kSweepTicks, 1, Mode::LiveBurst);
    ASSERT_FALSE(b.threw) << "corpus " << id << " built once and threw once: "
                          << b.error;
    ++checked;
    if (!a.emitted.empty()) ++emitted;
    {
      bool varies = false;
      for (std::size_t i = 1; i < a.emitted.size(); ++i)
        if (!sameValue(a.emitted[i], a.emitted[0])) { varies = true; break; }
      if (varies) ++sensitive;
    }
    if (!sameSequence(a.emitted, b.emitted) && differing.size() < 12) {
      std::ostringstream os;
      os << "\n  corpus_id " << id
         << "\n     drained: " << render(a.emitted)
         << "\n     live:    " << render(b.emitted);
      differing.push_back(os.str());
    } else if (!sameSequence(a.emitted, b.emitted)) {
      differing.push_back("\n  corpus_id " + std::to_string(id));
    }
  }

  std::cout << "[ENC-1335 sweep] checked=" << checked << " emitting=" << emitted
            << " timer-driven(excluded)=" << timerDriven
            << " build-refused=" << refused
            << " sensitive(non-constant drained sequence)=" << sensitive
            << " differing=" << differing.size() << "\n";

  EXPECT_GE(emitted, 150u)
      << "only " << emitted << " of " << checked << " entries emitted "
         "anything; a sweep over silent DAGs proves nothing";
  EXPECT_GE(sensitive, 30u)
      << "only " << sensitive << " of " << checked << " entries emit a "
         "NON-CONSTANT sequence over " << kSweepTicks
         << " ticks. A mistimed sample of a constant value is the same value, "
            "so `differing == 0` below would be vacuous. Measured at 159 when "
            "this test was written.";

  std::string detail;
  for (const auto& d : differing) detail += d;
  EXPECT_TRUE(differing.empty())
      << differing.size() << " of " << checked
      << " corpus entries emit a different sequence when ingress does not wait "
         "for the DAG than when it does. Each one is a value whose sample was "
         "taken at the pool's convenience:" << detail;
}
