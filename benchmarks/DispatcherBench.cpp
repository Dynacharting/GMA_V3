#include <benchmark/benchmark.h>
#include "krn/Dispatcher.hpp"
#include "krn/rt/ThreadPool.hpp"
#include "krn/AtomicStore.hpp"
#include "krn/Event.hpp"
#include "krn/nodes/INode.hpp"
#include <rapidjson/document.h>
#include <memory>

namespace {

class NullNode final : public krn::INode {
public:
    void onValue(const krn::StreamValue&) override {}
    void shutdown() noexcept override {}
};

static krn::Event makeTick(const std::string& symbol, double price) {
    auto doc = std::make_shared<rapidjson::Document>();
    doc->SetObject();
    auto& alloc = doc->GetAllocator();
    doc->AddMember("price", rapidjson::Value(price), alloc);
    return krn::Event{symbol, std::move(doc)};
}

} // namespace

static void BM_DispatcherOnTick(benchmark::State& state) {
    krn::rt::ThreadPool pool(2);
    krn::AtomicStore store;
    krn::Dispatcher md(&pool, &store);

    auto listener = std::make_shared<NullNode>();
    md.registerListener("BENCH", "price", listener);

    // Warm up with some history
    for (int i = 0; i < 100; ++i) {
        md.onTick(makeTick("BENCH", 100.0 + i * 0.1));
    }
    pool.drain();

    double price = 200.0;
    for (auto _ : state) {
        md.onTick(makeTick("BENCH", price));
        price += 0.01;
    }
    pool.drain();
    pool.shutdown();

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_DispatcherOnTick)->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
