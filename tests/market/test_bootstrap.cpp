// krn_market_tests' bootstrap (ENC-1562; krn_tests has its own, engine-only,
// at tests/test_bootstrap.cpp). Registers engine builtins (worker functions and node types), constructs a
// process-static MarketConnector, and calls its registerWith() — NOT start().
// Tests don't need live sockets, so the feed server / WS clients are
// constructed but never started. The "tick" event
// computer factory is registered with EventComputerRegistry as a side effect
// of registerWith, so every Dispatcher built in any test picks up its own
// fresh MarketTickComputer the first time it sees a tick.

#include "krn/AtomicStore.hpp"
#include "krn/Dispatcher.hpp"
#include "krn/FunctionMap.hpp"
#include "krn/FunctionRegistry.hpp"
#include "krn/NodeRegistry.hpp"
#include "krn/atomic/AtomicProviderRegistry.hpp"
#include "krn/engine/EventComputerRegistry.hpp"
#include "krn/engine/Registries.hpp"
#include "krn/market/MarketConnector.hpp"
#include "krn/rt/ThreadPool.hpp"
#include "krn/runtime/ShutdownCoordinator.hpp"
#include "krn/util/Config.hpp"
#include "krn/util/Logger.hpp"

#include "../support/CorpusPath.hpp"

#include <boost/asio/io_context.hpp>
#include <gtest/gtest.h>

#include <cstdio>
#include <memory>

namespace {

// Process-static singletons so the connector's captured pointers stay valid
// for the entire test-binary lifetime.
struct TestFixtureGlobals {
  bool                          built = false;
  krn::util::Config             cfg;
  std::unique_ptr<krn::rt::ThreadPool>   pool;
  std::unique_ptr<krn::AtomicStore>      store;
  std::unique_ptr<krn::Dispatcher>       dispatcher;
  std::unique_ptr<krn::rt::ShutdownCoordinator> shutdown;
  std::unique_ptr<boost::asio::io_context> ioc;
  std::unique_ptr<krn::market::MarketConnector> market;
};

TestFixtureGlobals& globals() {
  static TestFixtureGlobals g;
  return g;
}

class BuiltinsEnvironment : public ::testing::Environment {
public:
  // ENC-1102: SetUp() normally runs exactly once per process — gtest only
  // recreates global environments between --gtest_repeat iterations when
  // --gtest_recreate_environments_when_repeating is set, and that defaults to
  // false. It is one flag away from running per iteration though, so make it
  // idempotent rather than merely lucky.
  void SetUp() override {
    // ── ENC-1340: say it ONCE, at the top, in the words of the actual problem.
    //
    // `corpus_requests.json` is consumed by five test files. When it cannot be
    // found, those files produce about a dozen failures that each look like a
    // TreeBuilder bug, plus a sanitizer-flavoured warning — a false negative
    // wearing a sanitizer's authority. ENC-1338 spent an hour inside that
    // before working out the corpus was simply not where the process was
    // looking, and the cause was nothing more than having run the raw binary
    // from the wrong working directory.
    //
    // The resolver no longer depends on the cwd at all (see
    // tests/support/CorpusPath.hpp), so reaching this branch now means the
    // corpus is genuinely absent — a real build/packaging failure. Report it
    // here, before the first test runs, so the answer is the first thing on
    // stderr instead of an inference from a dozen reds. Deliberately NOT a
    // fatal gtest failure: a run filtered to tests that never touch the corpus
    // is legitimate, and those tests still deserve to run.
    if (!krn::testsupport::corpusRequestsFound()) {
      std::fputs("\n"
                 "========================================================\n"
                 "KRN TEST BOOTSTRAP — corpus_requests.json NOT FOUND\n"
                 "Every corpus-backed test below will fail for THIS reason\n"
                 "and for no other. Do not debug them individually.\n"
                 "========================================================\n",
                 stderr);
      std::fputs(krn::testsupport::corpusNotFoundDiagnostic().c_str(), stderr);
      std::fputs("========================================================\n\n",
                 stderr);
    }

    auto& g = globals();

    // Build the process-static objects once. They are documented above as
    // living for the whole binary because the connector's registrations close
    // over them; rebuilding them would tear down a live ThreadPool/Dispatcher
    // mid-binary for no gain.
    if (!g.built) {
      g.pool       = std::make_unique<krn::rt::ThreadPool>(1);
      g.store      = std::make_unique<krn::AtomicStore>();
      g.dispatcher = std::make_unique<krn::Dispatcher>(g.pool.get(), g.store.get(), g.cfg);
      g.shutdown   = std::make_unique<krn::rt::ShutdownCoordinator>();
      g.ioc        = std::make_unique<boost::asio::io_context>();
      g.market     = std::make_unique<krn::market::MarketConnector>();
      g.built      = true;
    }

    // Drop the one registry whose registration is ADDITIVE before re-running
    // the registrations below. EventComputerRegistry::registerFactory appends
    // by design ("Multiple factories per type are retained in registration
    // order"), so a second registerWith() leaves TWO "tick" computers — every
    // tick's atomics computed twice by two MarketTickComputers with two
    // independent TA histories. tests/market/ConnectorLifecycleRealTest.cpp
    // documents that exact corruption as the reason it must not call
    // registerWith() a second time; this keeps the invariant it relies on true
    // no matter how often SetUp() runs. Pinned by
    // tests/market/BootstrapIdempotenceTest.cpp.
    //
    // Nothing else here needs resetting — those registrations are already
    // idempotent, and re-running them repairs any registry a test cleared:
    //   FunctionMap::registerFunction, AtomicProviderRegistry::registerNamespace
    //       -> replace in place
    //   NodeTypeRegistry::registerNodeType, EventTypeRegistry::registerEvent,
    //   IngressRegistry::registerIngress
    //       -> first-wins; the duplicate returns false and changes nothing
    krn::engine::EventComputerRegistry::clear();

    krn::registerBuiltinFunctions();
    krn::registerBuiltinNodeTypes();

    krn::engine::EngineRegistries regs{
      &g.cfg, g.pool.get(), g.store.get(), g.dispatcher.get(),
      g.shutdown.get(), g.ioc.get(),
      &krn::engine::EventTypeRegistry::singleton(),
      &krn::engine::EventComputerRegistry::singleton(),
      &krn::engine::NodeTypeRegistry::singleton(),
      &krn::engine::IngressRegistry::singleton(),
      &krn::engine::ConfigNamespaceRegistry::singleton(),
      &krn::AtomicProviderRegistry::singleton(),
      &krn::FunctionMap::instance(),
      &krn::util::logger(),
    };

    g.market->registerWith(regs);   // NOTE: no start() — tests do not run sockets.
  }
};

::testing::Environment* const kBuiltinsEnv =
    ::testing::AddGlobalTestEnvironment(new BuiltinsEnvironment);

} // namespace
