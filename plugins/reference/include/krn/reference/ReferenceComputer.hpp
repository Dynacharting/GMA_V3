#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "krn/engine/IEventComputer.hpp"

namespace krn::synthetic {

// ENC-1555 (SPEC specs/2026-10-15-gma-engine-purity D13, the Q6 ruling).
//
// A deterministic, history-dependent computer whose OUTPUT SHAPE IS DECLARED
// HERE, so a test can assert the shape by name instead of copying it from
// another computer. Per stream key, for the n-th event of type `kEventType`
// that carries a numeric `kInputField` x (n counted from 0):
//
//     S_n = x_0 + x_1 + ... + x_n            written to `kSumKey`
//     Q_n = x_0*x_0 + ... + x_n*x_n          written to `kSumSqKey`
//
//   n == 0                  nothing is written: both keys are ABSENT
//   1 <= n <= kWarmupTicks  both keys are NaN
//   n >  kWarmupTicks       both keys are S_n / Q_n, finite
//
// For x > 1 on every event, S_n, Q_n and Q_n - S_n are each strictly
// increasing in n, so each takes a DISTINCT value on every post-warm-up event:
// a value read on any other event than its own differs from the right one.
//
// Plain sequential accumulation in event order, in double. Integer inputs
// below 2^26 sum exactly, so the output is identical on every run.
// An event without a numeric `kInputField` is ignored and does not advance n.
class ReferenceComputer final : public engine::IEventComputer {
public:
  static constexpr std::string_view kEventType   = "reference.tick";
  static constexpr std::string_view kInputField  = "sample";
  static constexpr std::string_view kSumKey      = "reference.sum";
  static constexpr std::string_view kSumSqKey    = "reference.sumsq";
  static constexpr std::size_t      kWarmupTicks = 3;

  std::string_view eventType() const override { return kEventType; }
  void compute(const std::string& symbol,
               const engine::FieldSource& fields,
               engine::ComputeContext& ctx) override;

private:
  struct History {
    std::size_t events { 0 };
    double      sum    { 0.0 };
    double      sumSq  { 0.0 };
  };
  // compute() may be entered concurrently (IEventComputer's contract).
  std::mutex                               mx_;
  std::unordered_map<std::string, History> byStream_;
};

} // namespace krn::synthetic
