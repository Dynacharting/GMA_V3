#include "krn/reference/ReferenceComputer.hpp"

#include <limits>

#include "krn/AtomicStore.hpp"

namespace krn::synthetic {

void ReferenceComputer::compute(const std::string& symbol,
                                const engine::FieldSource& fields,
                                engine::ComputeContext& ctx) {
  if (!ctx.store) return;
  static const std::string kIn(kInputField);
  static const std::string kSum(kSumKey);
  static const std::string kSumSq(kSumSqKey);

  double x = 0.0;
  if (!fields.tryGetNumber(kIn, x)) return;

  std::size_t n = 0;
  double sum = 0.0, sumSq = 0.0;
  {
    std::lock_guard<std::mutex> lk(mx_);
    History& h = byStream_[symbol];
    n = h.events++;
    h.sum   += x;
    h.sumSq += x * x;
    sum   = h.sum;
    sumSq = h.sumSq;
  }

  if (n == 0) return;
  if (n <= kWarmupTicks) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    ctx.store->set(symbol, kSum, nan);
    ctx.store->set(symbol, kSumSq, nan);
    return;
  }
  ctx.store->set(symbol, kSum, sum);
  ctx.store->set(symbol, kSumSq, sumSq);
}

} // namespace krn::synthetic
