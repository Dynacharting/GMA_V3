#include "krn/nodes/Listener.hpp"
#include "krn/Dispatcher.hpp"
#include "krn/rt/ThreadPool.hpp"
#include "krn/AtomicStore.hpp"
#include "krn/StreamValue.hpp"
#include "krn/nodes/INode.hpp"
#include "krn/atomic/AtomicProviderRegistry.hpp"
#include "krn/reference/ReferenceComputer.hpp"
#include "krn/reference/ReferenceConnector.hpp"
#include "krn/engine/Registries.hpp"
#include "../support/ScopedPipelineOnly.hpp"
#include <boost/asio/io_context.hpp>
#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <atomic>
#include <mutex>

using namespace krn;
using namespace krn::nodes;

namespace {

class DownstreamStub : public INode {
public:
    std::mutex mx;
    std::vector<StreamValue> received;
    std::atomic<int> count{0};
    void onValue(const StreamValue& sv) override {
        {
            std::lock_guard<std::mutex> lk(mx);
            received.push_back(sv);
        }
        count.fetch_add(1, std::memory_order_release);
    }
    size_t safeSize() {
        std::lock_guard<std::mutex> lk(mx);
        return received.size();
    }
    void shutdown() noexcept override {}
};

} // anonymous namespace

TEST(ListenerTest, ForwardsValueToDownstreamViaPool) {
    rt::ThreadPool pool(1);
    AtomicStore store;
    Dispatcher dispatcher(&pool, &store);

    auto stub = std::make_shared<DownstreamStub>();
    auto listener = std::make_shared<Listener>("SYM", "field", stub, &pool, &dispatcher);
    listener->start();

    listener->onValue(StreamValue{"SYM", 1.0});
    listener->onValue(StreamValue{"SYM", 2.0});
    listener->onValue(StreamValue{"SYM", 3.0});

    pool.shutdown();

    ASSERT_EQ(stub->safeSize(), 3u);
    EXPECT_DOUBLE_EQ(std::get<double>(stub->received[0].value), 1.0);
    EXPECT_DOUBLE_EQ(std::get<double>(stub->received[1].value), 2.0);
    EXPECT_DOUBLE_EQ(std::get<double>(stub->received[2].value), 3.0);
}

TEST(ListenerTest, ShutdownStopsPropagation) {
    rt::ThreadPool pool(1);
    AtomicStore store;
    Dispatcher dispatcher(&pool, &store);

    auto stub = std::make_shared<DownstreamStub>();
    auto listener = std::make_shared<Listener>("X", "field", stub, &pool, &dispatcher);
    listener->start();

    listener->onValue(StreamValue{"X", 10.0});
    pool.drain();
    EXPECT_EQ(stub->count.load(), 1);

    listener->shutdown();
    stub->received.clear();
    stub->count = 0;

    listener->onValue(StreamValue{"X", 20.0});
    pool.shutdown();
    EXPECT_EQ(stub->count.load(), 0);
}

TEST(ListenerTest, SymbolAndFieldAccessors) {
    rt::ThreadPool pool(1);
    AtomicStore store;
    Dispatcher dispatcher(&pool, &store);

    auto stub = std::make_shared<DownstreamStub>();
    auto listener = std::make_shared<Listener>("AAPL", "price", stub, &pool, &dispatcher);

    EXPECT_EQ(listener->symbol(), "AAPL");
    EXPECT_EQ(listener->field(), "price");
    pool.shutdown();
}

TEST(ListenerTest, HandlesRapidDispatch) {
    rt::ThreadPool pool(2);
    AtomicStore store;
    Dispatcher dispatcher(&pool, &store);

    auto stub = std::make_shared<DownstreamStub>();
    auto listener = std::make_shared<Listener>("R", "field", stub, &pool, &dispatcher);
    listener->start();

    const int sends = 50;
    for (int i = 0; i < sends; ++i) {
        listener->onValue(StreamValue{"R", static_cast<double>(i)});
    }

    pool.shutdown();
    EXPECT_EQ(stub->safeSize(), static_cast<size_t>(sends));
}

TEST(ListenerTest, NoCrashOnDestructionWithoutStart) {
    {
        rt::ThreadPool pool(1);
        AtomicStore store;
        Dispatcher dispatcher(&pool, &store);
        auto stub = std::make_shared<DownstreamStub>();
        auto listener = std::make_shared<Listener>("E", "field", stub, &pool, &dispatcher);
        // Deliberately not calling start()
        listener->shutdown();
    }
    SUCCEED();
}

// --------------------------------------------------------------------
// Listener::Create factory — ENC-101 push-vs-pull rule enforcement.
// See GMA_V3/docs/atomic-keys.md and the spec at
// GMA_V3/specs/2026-05-06-ob-keys-pipeline-only/.
// --------------------------------------------------------------------

TEST(ListenerTest, RejectsObNamespaceAtFactory) {
    const krn::testsupport::ScopedPipelineOnly ob("ob");
    rt::ThreadPool pool(1);
    AtomicStore store;
    Dispatcher dispatcher(&pool, &store);

    auto stub = std::make_shared<DownstreamStub>();
    auto res = Listener::Create("NEXO", "ob.best.bid.price", stub, &pool, &dispatcher);

    ASSERT_FALSE(res.has_value()) << "Listener::Create must reject ob.* fields";
    const auto& msg = res.error().message;
    EXPECT_NE(msg.find("pipeline-only"), std::string::npos)
        << "error message must contain literal 'pipeline-only'; got: " << msg;
    EXPECT_NE(msg.find("ob.best.bid.price"), std::string::npos)
        << "error message must echo the offending field name; got: " << msg;
    EXPECT_NE(msg.find("docs/atomic-keys.md"), std::string::npos)
        << "error message must point at docs/atomic-keys.md; got: " << msg;
    pool.shutdown();
}

TEST(ListenerTest, RejectsObSpread) {
    const krn::testsupport::ScopedPipelineOnly ob("ob");
    rt::ThreadPool pool(1);
    AtomicStore store;
    Dispatcher dispatcher(&pool, &store);

    auto stub = std::make_shared<DownstreamStub>();
    auto res = Listener::Create("NEXO", "ob.spread", stub, &pool, &dispatcher);

    ASSERT_FALSE(res.has_value());
    EXPECT_NE(res.error().message.find("pipeline-only"), std::string::npos);
    EXPECT_NE(res.error().message.find("ob.spread"), std::string::npos);
    pool.shutdown();
}

TEST(ListenerTest, AcceptsBareKeyAtFactory) {
    rt::ThreadPool pool(1);
    AtomicStore store;
    Dispatcher dispatcher(&pool, &store);

    auto stub = std::make_shared<DownstreamStub>();
    auto res = Listener::Create("AAPL", "lastPrice", stub, &pool, &dispatcher);

    ASSERT_TRUE(res.has_value()) << "bare key 'lastPrice' should pass";
    auto listener = res.value();
    EXPECT_EQ(listener->symbol(), "AAPL");
    EXPECT_EQ(listener->field(), "lastPrice");
    // Create() returns an already-started Listener; values propagate.
    listener->onValue(StreamValue{"AAPL", 42.0});
    pool.shutdown();
    EXPECT_EQ(stub->safeSize(), 1u);
    EXPECT_DOUBLE_EQ(std::get<double>(stub->received[0].value), 42.0);
}

TEST(ListenerTest, FactoryDoesNotMatchObesityFalsePositive) {
    // Defensive: the namespace is the text before the first '.', matched
    // exactly — not a starts_with("ob") call. Field names like "obesity" or
    // "obvious" must NOT be rejected.
    const krn::testsupport::ScopedPipelineOnly ob("ob");
    rt::ThreadPool pool(1);
    AtomicStore store;
    Dispatcher dispatcher(&pool, &store);

    auto stub = std::make_shared<DownstreamStub>();
    auto res = Listener::Create("X", "obesity", stub, &pool, &dispatcher);
    EXPECT_TRUE(res.has_value());
    pool.shutdown();
}

// --------------------------------------------------------------------
// ENC-1575: the pipeline-only rule is a namespace flag a plugin sets in
// registerWith(), not a spelling inside Listener.
// --------------------------------------------------------------------

namespace {

Result<std::shared_ptr<Listener>> createOn(const std::string& field,
                                           rt::ThreadPool& pool,
                                           Dispatcher& dispatcher) {
    return Listener::Create("REF", field, std::make_shared<DownstreamStub>(),
                            &pool, &dispatcher);
}

} // anonymous namespace

TEST(ListenerTest, ReferencePluginDeclaresItsNamespacePipelineOnly) {
    using synthetic::ReferenceComputer;
    using synthetic::ReferenceConnector;
    rt::ThreadPool pool(1);
    AtomicStore store;
    Dispatcher dispatcher(&pool, &store);
    boost::asio::io_context ioc;
    const std::string sumKey(ReferenceComputer::kSumKey);
    const std::string ns(ReferenceConnector::kPipelineOnlyNamespace);
    ASSERT_EQ(sumKey.rfind(ns + ".", 0), 0u)
        << "the declared namespace must be the one ReferenceComputer writes";

    // Before the plugin registers, nothing declares `reference` pipeline-only.
    ASSERT_FALSE(AtomicProviderRegistry::isPipelineOnlyKey(sumKey));
    EXPECT_TRUE(createOn(sumKey, pool, dispatcher).has_value());

    engine::EngineRegistries regs;
    regs.pool       = &pool;
    regs.store      = &store;
    regs.dispatcher = &dispatcher;
    regs.io         = &ioc;
    regs.computers  = &engine::EventComputerRegistry::singleton();
    regs.protocols  = &engine::ProtocolIngressRegistry::singleton();
    ReferenceConnector connector;
    connector.registerWith(regs);

    auto refused = createOn(sumKey, pool, dispatcher);
    ASSERT_FALSE(refused.has_value())
        << "a Listener on the reference plugin's pipeline-only namespace must be refused";
    const auto& msg = refused.error().message;
    EXPECT_NE(msg.find("pipeline-only"), std::string::npos) << msg;
    EXPECT_NE(msg.find(sumKey), std::string::npos) << msg;
    EXPECT_NE(msg.find("docs/atomic-keys.md"), std::string::npos) << msg;
    EXPECT_FALSE(createOn(std::string(ReferenceComputer::kSumSqKey), pool, dispatcher).has_value());

    // Unflagged namespaces and bare keys are accepted.
    EXPECT_TRUE(createOn("synthetic.sin", pool, dispatcher).has_value());
    EXPECT_TRUE(createOn(std::string(ReferenceComputer::kInputField), pool, dispatcher).has_value());
    EXPECT_TRUE(createOn("referenced.sum", pool, dispatcher).has_value());

    connector.stop();
    EXPECT_TRUE(AtomicProviderRegistry::unmarkPipelineOnly(ns));
    EXPECT_TRUE(createOn(sumKey, pool, dispatcher).has_value());
    pool.shutdown();
}

TEST(ListenerTest, PipelineOnlyNamespaceMatchIsExact) {
    rt::ThreadPool pool(1);
    AtomicStore store;
    Dispatcher dispatcher(&pool, &store);
    const krn::testsupport::ScopedPipelineOnly ob("ob");
    ASSERT_TRUE(ob.marked());

    for (const char* refused : {"ob.x", "ob.", "ob.best.bid.price"}) {
        EXPECT_FALSE(createOn(refused, pool, dispatcher).has_value())
            << "flagged `ob` must refuse '" << refused << "'";
    }
    for (const char* accepted : {"obsolete", "obx.y", "obx.", "ob", "o.b", "xob.y", "x.ob.y", ".ob"}) {
        EXPECT_TRUE(createOn(accepted, pool, dispatcher).has_value())
            << "flagged `ob` must not refuse '" << accepted << "'";
    }
    pool.shutdown();
}

TEST(ListenerTest, PipelineOnlyRejectsEmptyOrDottedNamespace) {
    EXPECT_FALSE(AtomicProviderRegistry::markPipelineOnly(""));
    EXPECT_FALSE(AtomicProviderRegistry::markPipelineOnly("ob.best"));
    EXPECT_FALSE(AtomicProviderRegistry::isPipelineOnlyKey("ob.best.bid"));
    EXPECT_FALSE(AtomicProviderRegistry::isPipelineOnlyKey(".x"));
}

TEST(ListenerTest, UnflaggedObIsAcceptedWithoutAPlugin) {
    // krn carries no market plugin: until one declares `ob`, it is an ordinary
    // namespace. krn-common's MarketConnector::registerWith declares it.
    rt::ThreadPool pool(1);
    AtomicStore store;
    Dispatcher dispatcher(&pool, &store);
    ASSERT_FALSE(AtomicProviderRegistry::isPipelineOnlyKey("ob.spread"));
    EXPECT_TRUE(createOn("ob.spread", pool, dispatcher).has_value());
    pool.shutdown();
}
