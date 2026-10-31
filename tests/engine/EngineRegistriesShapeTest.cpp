// Pins the shape of the EngineRegistries struct: every field a connector
// might rely on must be populatable. The composition root in main.cpp wires
// real instances; this test wires every field with non-null pointers and
// verifies they round-trip.

#include "krn/AtomicStore.hpp"
#include "krn/Dispatcher.hpp"
#include "krn/FunctionMap.hpp"
#include "krn/atomic/AtomicProviderRegistry.hpp"
#include "krn/engine/Registries.hpp"
#include "krn/rt/ThreadPool.hpp"
#include "krn/runtime/ShutdownCoordinator.hpp"
#include "krn/util/Config.hpp"
#include "krn/util/Logger.hpp"

#include <boost/asio/io_context.hpp>
#include <gtest/gtest.h>

using namespace krn;

TEST(EngineRegistriesShapeTest, AllFieldsPopulatable) {
  util::Config cfg;
  rt::ThreadPool pool(1);
  AtomicStore store;
  Dispatcher dispatcher(&pool, &store);
  rt::ShutdownCoordinator shutdown;
  boost::asio::io_context ioc;

  engine::EngineRegistries regs{
    &cfg, &pool, &store, &dispatcher, &shutdown, &ioc,
    &engine::EventTypeRegistry::singleton(),
    &engine::EventComputerRegistry::singleton(),
    &engine::NodeTypeRegistry::singleton(),
    &engine::IngressRegistry::singleton(),
    &engine::ConfigNamespaceRegistry::singleton(),
    &AtomicProviderRegistry::singleton(),
    &FunctionMap::instance(),
    &util::logger(),
    &engine::ProtocolIngressRegistry::singleton(),
  };

  // Per-instance engine objects.
  EXPECT_EQ(regs.cfg, &cfg);
  EXPECT_EQ(regs.pool, &pool);
  EXPECT_EQ(regs.store, &store);
  EXPECT_EQ(regs.dispatcher, &dispatcher);
  EXPECT_EQ(regs.shutdown, &shutdown);
  EXPECT_EQ(regs.io, &ioc);

  // Extension-point registries.
  EXPECT_NE(regs.events,    nullptr);
  EXPECT_NE(regs.computers, nullptr);
  EXPECT_NE(regs.nodes,     nullptr);
  EXPECT_NE(regs.ingress,   nullptr);
  EXPECT_NE(regs.configNs,  nullptr);
  EXPECT_NE(regs.providers, nullptr);
  EXPECT_NE(regs.functions, nullptr);
  EXPECT_NE(regs.log,       nullptr);
  EXPECT_NE(regs.protocols, nullptr);

  // Singleton accessors must return stable references across calls.
  EXPECT_EQ(&engine::EventTypeRegistry::singleton(),     &engine::EventTypeRegistry::singleton());
  EXPECT_EQ(&engine::EventComputerRegistry::singleton(), &engine::EventComputerRegistry::singleton());
  EXPECT_EQ(&engine::NodeTypeRegistry::singleton(),      &engine::NodeTypeRegistry::singleton());
  EXPECT_EQ(&engine::IngressRegistry::singleton(),       &engine::IngressRegistry::singleton());
  EXPECT_EQ(&engine::ConfigNamespaceRegistry::singleton(),&engine::ConfigNamespaceRegistry::singleton());
  EXPECT_EQ(&AtomicProviderRegistry::singleton(),        &AtomicProviderRegistry::singleton());
  EXPECT_EQ(&engine::ProtocolIngressRegistry::singleton(),&engine::ProtocolIngressRegistry::singleton());
}

TEST(EngineRegistriesShapeTest, FieldCountIs15) {
  // Guard against accidental field removal: sizeof should equal 15 pointers.
  // (Six per-instance + nine registry pointers, all 8-byte on x86_64.)
  static_assert(sizeof(engine::EngineRegistries) == 15 * sizeof(void*),
                "EngineRegistries must carry exactly 15 pointer-sized fields");
  SUCCEED();
}
