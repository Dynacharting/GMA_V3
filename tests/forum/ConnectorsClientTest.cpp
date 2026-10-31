// Tests the JSON-to-ingress translation done by
// krn::forum::ConnectorsClient::parseConnectorsJson. Decoupled from
// the network — fed directly with the wire shape forum's
// /api/connectors returns (cf.
// forum/internal/http/routes/connectors.go ListConnectors), so any
// drift in that endpoint surfaces here.

#include "krn/forum/ConnectorsClient.hpp"

#include "krn/engine/EngineRegistries.hpp"
#include "krn/engine/ProtocolIngressRegistry.hpp"
#include "krn/reference/ReferenceConnector.hpp"
#include "krn/util/Logger.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

using krn::forum::ConnectorsClient;
using krn::synthetic::ReferenceConnector;
using Ingress = krn::util::Config::Ingress;

namespace {

// ENC-1574: the protocol -> ingress-kind mapping is registered by a plugin,
// here the reference plugin, through the EngineRegistries it is handed.
void registerReferenceProtocol() {
  krn::engine::EngineRegistries regs;
  regs.protocols = &krn::engine::ProtocolIngressRegistry::singleton();
  ReferenceConnector::registerProtocols(regs);
}

const std::string kProtocol(ReferenceConnector::kProtocol);
const std::string kKind(ReferenceConnector::kIngressKind);

std::string row(const std::string& name, const std::string& endpoint,
                const std::string& protocol, const std::string& symbols) {
  return R"({"name": ")" + name + R"(", "endpointUrl": ")" + endpoint +
         R"(", "protocol": ")" + protocol + R"(", "symbols": )" + symbols + "}";
}

}  // namespace

TEST(ConnectorsClientTest, MapsRegisteredProtocolRowToRegisteredKind) {
  registerReferenceProtocol();
  const std::string body = R"([{
    "id": "84ca9d62-e0eb-5818-ab2f-d0fee8530e60",
    "name": "feed-simulator",
    "authMethod": "websocket",
    "endpointUrl": "wss://feed-sim.v3m.xyz/feed",
    "protocol": ")" + kProtocol + R"(",
    "symbols": ["NEXO","VALT","RAYM","STRT","DRRB"],
    "status": "connected",
    "resources": [
      {"name": "Equity Ticks", "fields": []}
    ]
  }])";
  auto ingresses = ConnectorsClient::parseConnectorsJson(body);
  ASSERT_EQ(ingresses.size(), 1u);
  const auto& ing = ingresses[0];

  EXPECT_EQ(ing.kind, kKind);

  auto urlIt = ing.params.find("url");
  ASSERT_NE(urlIt, ing.params.end());
  EXPECT_EQ(urlIt->second, "wss://feed-sim.v3m.xyz/feed");

  auto adapterIt = ing.params.find("adapter");
  ASSERT_NE(adapterIt, ing.params.end());
  EXPECT_EQ(adapterIt->second, kProtocol);

  auto symbolsIt = ing.params.find("symbols");
  ASSERT_NE(symbolsIt, ing.params.end());
  EXPECT_EQ(symbolsIt->second, "NEXO,VALT,RAYM,STRT,DRRB");
}

TEST(ConnectorsClientTest, RefusesAndLogsUnregisteredProtocol) {
  registerReferenceProtocol();
  // No plugin in this binary registers either protocol, so neither may map.
  ASSERT_FALSE(krn::engine::ProtocolIngressRegistry::contains("itch"));
  ASSERT_FALSE(krn::engine::ProtocolIngressRegistry::contains("binance-ws"));

  const std::string body = "[" +
      row("ref", "wss://x/feed", kProtocol, R"(["A"])") + "," +
      row("itch-feed", "wss://y/feed", "itch", R"(["B"])") + "," +
      row("binance", "wss://stream.binance.com/ws", "binance-ws", R"(["BTC"])") + "]";

  const char* path = "test_connectors_client_enc1574.log";
  std::remove(path);
  krn::util::logger().setFile(path);
  auto ingresses = ConnectorsClient::parseConnectorsJson(body);
  krn::util::logger().setFile("");

  std::ifstream f(path);
  const std::string log((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  f.close();
  std::remove(path);

  ASSERT_EQ(ingresses.size(), 1u);
  EXPECT_EQ(ingresses[0].kind, kKind);
  EXPECT_EQ(ingresses[0].params.at("url"), "wss://x/feed");

  // One refusal line per unregistered row, each naming its protocol.
  std::size_t refusals = 0;
  for (auto pos = log.find("forum.connectors.unknown_protocol"); pos != std::string::npos;
       pos = log.find("forum.connectors.unknown_protocol", pos + 1)) {
    ++refusals;
  }
  EXPECT_EQ(refusals, 2u) << log;
  EXPECT_NE(log.find("itch"), std::string::npos) << log;
  EXPECT_NE(log.find("binance-ws"), std::string::npos) << log;
}

TEST(ConnectorsClientTest, SkipsConnectorsWithEmptyEndpoint) {
  registerReferenceProtocol();
  const std::string body = "[" + row("stub", "", kProtocol, R"(["A"])") + "]";
  auto ingresses = ConnectorsClient::parseConnectorsJson(body);
  EXPECT_EQ(ingresses.size(), 0u);
}

TEST(ConnectorsClientTest, EmptySymbolsArrayDefaultsToWildcard) {
  registerReferenceProtocol();
  const std::string body = "[" + row("feed-simulator", "wss://x/feed", kProtocol, "[]") + "]";
  auto ingresses = ConnectorsClient::parseConnectorsJson(body);
  ASSERT_EQ(ingresses.size(), 1u);
  EXPECT_EQ(ingresses[0].params.at("symbols"), "*");
}

TEST(ConnectorsClientTest, RejectsNonArrayPayload) {
  // forum returns {"error":"..."} on auth failures; not a list — must throw.
  EXPECT_THROW(
      ConnectorsClient::parseConnectorsJson(R"({"error":"unauthorized"})"),
      std::runtime_error);
}

TEST(ConnectorsClientTest, RejectsMalformedJson) {
  EXPECT_THROW(
      ConnectorsClient::parseConnectorsJson("not-json-at-all"),
      std::runtime_error);
}

TEST(ConnectorsClientTest, EmptyArrayProducesNoIngresses) {
  auto ingresses = ConnectorsClient::parseConnectorsJson("[]");
  EXPECT_EQ(ingresses.size(), 0u);
}
