// ENC-1595: a parked config key that no engine field, ConfigNamespaceRegistry
// reader or markConsumed() call claims is reported once, by name and file,
// instead of being dropped silently. krn_host runs the steps in this order:
// loadFromFile -> applyExtraArgs -> registerWith -> afterConnectorsRegistered
// -> dispatchPendingKeys -> reportUnclaimedKeys.

#include "krn/engine/ConfigNamespaceRegistry.hpp"
#include "krn/host/Host.hpp"
#include "krn/util/Config.hpp"
#include "krn/util/Logger.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

std::string writeTemp(const std::string& body) {
  char tmpl[] = "/tmp/krn_enc1595_XXXXXX";
  int fd = ::mkstemp(tmpl);
  if (fd >= 0) ::close(fd);
  std::ofstream f(tmpl);
  f << body;
  return tmpl;
}

// ENC-1102: a per-invocation namespace prefix, so --gtest_repeat stays green.
std::string uniquePrefix() {
  static std::atomic<int> n{0};
  return "enc1595ns" + std::to_string(n.fetch_add(1));
}

std::vector<std::string> keysOf(const krn::util::Config& cfg) {
  std::vector<std::string> out;
  for (const auto& u : cfg.unclaimedKeys()) out.push_back(u.key);
  return out;
}

// Runs reportUnclaimedKeys() with the logger pointed at a file; returns the
// lines it wrote.
std::vector<std::string> reportLines(const krn::util::Config& cfg, std::size_t& count) {
  const std::string logPath = writeTemp("");
  krn::util::logger().setFile(logPath);
  count = cfg.reportUnclaimedKeys();
  krn::util::logger().setFile("");
  std::ifstream in(logPath);
  std::vector<std::string> lines;
  for (std::string l; std::getline(in, l);) lines.push_back(l);
  std::remove(logPath.c_str());
  return lines;
}

std::size_t countContaining(const std::vector<std::string>& lines, const std::string& s) {
  std::size_t n = 0;
  for (const auto& l : lines) n += l.find(s) != std::string::npos ? 1 : 0;
  return n;
}

} // namespace

TEST(ConfigUnclaimedKeysTest, MovedAndMisspelledKeysWarnOncePerKey) {
  const auto path = writeTemp(
      "wsPort=9100\n"
      "threadPoolSize=2\n"
      "allowNegativePrices=true\n"
      "threadPoolSzie=8\n"
      "threadPoolSzie=9\n");
  krn::util::Config cfg;
  ASSERT_TRUE(cfg.loadFromFile(path));
  cfg.dispatchPendingKeys();

  EXPECT_EQ(cfg.wsPort, 9100);
  EXPECT_EQ(keysOf(cfg), (std::vector<std::string>{"allowNegativePrices", "threadPoolSzie"}));
  for (const auto& u : cfg.unclaimedKeys()) EXPECT_EQ(u.file, path);

  std::size_t count = 0;
  const auto lines = reportLines(cfg, count);
  std::remove(path.c_str());
  EXPECT_EQ(count, 2u);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(countContaining(lines, "WARN  config.unclaimed_key key=allowNegativePrices file=" + path), 1u);
  EXPECT_EQ(countContaining(lines, "WARN  config.unclaimed_key key=threadPoolSzie file=" + path), 1u);
}

TEST(ConfigUnclaimedKeysTest, EngineKeysOnlyLogNothing) {
  const auto path = writeTemp(
      "# comment\n"
      "wsPort=9101\nthreadPoolSize=2\ntaHistoryMax=10\nmaxSymbols=5\n"
      "maxFieldsPerSymbol=5\natomicKeyNamespaceByField=false\n"
      "metricsEnabled=false\nmetricsIntervalSec=3\nlogLevel=info\n"
      "forumUrl=\nforumTenantId=\nforumAuthToken=\n"
      "ingress.0.kind=reference.ticker\ningress.0.rate=5\n");
  krn::util::Config cfg;
  ASSERT_TRUE(cfg.loadFromFile(path));
  std::remove(path.c_str());
  cfg.dispatchPendingKeys();

  EXPECT_TRUE(cfg.unclaimedKeys().empty());
  std::size_t count = 1;
  EXPECT_TRUE(reportLines(cfg, count).empty());
  EXPECT_EQ(count, 0u);
}

TEST(ConfigUnclaimedKeysTest, NamespaceReaderClaimsOnlyWhatItAccepts) {
  const std::string prefix = uniquePrefix();
  krn::engine::ConfigNamespaceRegistry::registerNamespace(prefix,
      [](std::string_view tail, std::string_view) { return tail == "known"; });

  const auto path = writeTemp(prefix + ".known=1\n" + prefix + ".unknown=2\n");
  krn::util::Config cfg;
  ASSERT_TRUE(cfg.loadFromFile(path));
  std::remove(path.c_str());

  EXPECT_EQ(cfg.dispatchPendingKeys(), 1u);
  EXPECT_EQ(keysOf(cfg), (std::vector<std::string>{prefix + ".unknown"}));
}

// The krn-common shape: a plugin re-reads the file in a HostHooks callback
// and marks the keys it used. Marked keys are silent whether the mark comes
// before dispatchPendingKeys (applyExtraArgs) or after it; the others warn.
TEST(ConfigUnclaimedKeysTest, KeysAHookMarksConsumedDoNotWarn) {
  const auto path = writeTemp(
      "wsPort=9102\n"
      "pluginPort=9001\n"
      "plugin.0.url=ws://x\n"
      "lateKey=1\n"
      "pluginPrt=9002\n");

  krn::host::HostHooks hooks;
  hooks.applyExtraArgs = [](int, char*[], krn::util::Config& cfg) {
    cfg.markConsumed("pluginPort");
    cfg.markConsumed("plugin.0.url");
    cfg.markConsumed("notInTheFile");
  };

  krn::util::Config cfg;
  ASSERT_TRUE(cfg.loadFromFile(path));
  std::remove(path.c_str());
  char arg0[] = "test";
  char* argv[] = {arg0, nullptr};
  hooks.applyExtraArgs(1, argv, cfg);
  cfg.dispatchPendingKeys();
  cfg.markConsumed("lateKey");

  EXPECT_EQ(keysOf(cfg), (std::vector<std::string>{"pluginPrt"}));
  std::size_t count = 0;
  const auto lines = reportLines(cfg, count);
  EXPECT_EQ(count, 1u);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(countContaining(lines, "key=pluginPrt"), 1u);
}
