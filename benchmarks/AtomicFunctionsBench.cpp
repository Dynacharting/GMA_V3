#include <benchmark/benchmark.h>
#include "krn/market/AtomicFunctions.hpp"
#include "krn/AtomicStore.hpp"
#include "krn/market/SymbolHistory.hpp"
#include <cmath>

static std::vector<krn::TickEntry> makeHistory(size_t n) {
    std::vector<krn::TickEntry> hist;
    for (size_t i = 0; i < n; ++i) {
        double price = 100.0 + 10.0 * std::sin(static_cast<double>(i) * 0.1);
        double volume = 1000.0 + 500.0 * std::cos(static_cast<double>(i) * 0.05);
        hist.push_back({price, volume});
    }
    return hist;
}

static void BM_ComputeAllAtomicValues(benchmark::State& state) {
    const size_t n = static_cast<size_t>(state.range(0));
    auto hist = makeHistory(n);
    krn::AtomicStore store;

    for (auto _ : state) {
        krn::computeAllAtomicValues("BENCH", hist, store);
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_ComputeAllAtomicValues)
    ->Arg(50)
    ->Arg(200)
    ->Arg(500)
    ->Arg(1000)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
