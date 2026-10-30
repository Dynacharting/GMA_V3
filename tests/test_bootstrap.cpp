// krn_tests' bootstrap: the ENGINE one (ENC-1562, purity SPEC D13).
//
// Registers the engine builtins (worker functions and node types) and nothing
// else. No connector is constructed and no event computer is registered, so a
// Dispatcher built in any test computes only what that test adds to it. The
// market bootstrap, which also registers MarketConnector, is
// tests/market/test_bootstrap.cpp and belongs to krn_market_tests.

#include "krn/FunctionRegistry.hpp"
#include "krn/NodeRegistry.hpp"
#include "krn/engine/EventComputerRegistry.hpp"

#include "support/CorpusPath.hpp"

#include <gtest/gtest.h>

#include <cstdio>

namespace {

class BuiltinsEnvironment : public ::testing::Environment {
public:
  // Idempotent: both registrations replace in place or are first-wins, so a
  // re-run under --gtest_recreate_environments_when_repeating changes nothing.
  void SetUp() override {
    // ENC-1340: a missing corpus is reported once, up front (see
    // tests/support/CorpusPath.hpp). Not fatal, so a filtered run of tests that
    // never read the corpus still runs.
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

    krn::registerBuiltinFunctions();
    krn::registerBuiltinNodeTypes();

    // The default event type is "tick" (Event.hpp). Nothing in this binary may
    // register a computer for it, or a test that names no connector would
    // silently route through one again.
    EXPECT_EQ(krn::engine::EventComputerRegistry::factoryCount("tick"), 0u)
        << "krn_tests' engine bootstrap found a \"tick\" event computer "
           "registered; this binary must carry none (ENC-1562)";
  }
};

::testing::Environment* const kBuiltinsEnv =
    ::testing::AddGlobalTestEnvironment(new BuiltinsEnvironment);

} // namespace
