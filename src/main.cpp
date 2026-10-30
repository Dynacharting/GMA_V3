// File: src/main.cpp — krn_server: krn_host + the market connector (ENC-1559,
// purity SPEC D6). The engine composition is in src/host/Host.cpp; this file
// holds only what belongs to the market connector: the feedPort argument
// (argv[3]), the legacy feed-key ingress synthesis, and the feedPort fields on
// the boot and listening log lines.
//
// CLI: krn_server [wsPort] [configFile] [feedPort]
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "krn/host/Host.hpp"
#include "krn/market/MarketConnector.hpp"
#include "krn/util/Config.hpp"
#include "krn/util/Logger.hpp"

static unsigned short parsePort(const char* str, unsigned short fallback) {
  try {
    unsigned long p = std::stoul(str);
    if (p == 0 || p > 65535) return fallback;
    return static_cast<unsigned short>(p);
  } catch (...) {
    return fallback;
  }
}

int main(int argc, char* argv[]) {
  using namespace krn::util;

  krn::host::HostOptions opts;
  opts.programName = "krn_server";
  opts.connectors.push_back([] { return std::make_unique<krn::market::MarketConnector>(); });

  // argv[3] = feedPort. CLI args win over the config file.
  opts.hooks.applyExtraArgs = [](int argc, char* argv[], Config& cfg) {
    if (cfg.feedPort <= 0 || cfg.feedPort > 65535) {
      std::cerr << "[config] warning: invalid feedPort=" << cfg.feedPort << ", using default 9001\n";
      cfg.feedPort = 9001;
    }
    unsigned short feedPort = static_cast<unsigned short>(cfg.feedPort);
    if (argc > 3) feedPort = parsePort(argv[3], feedPort);
    // ENC-1331: this must stay AHEAD of cfg.synthesizeIngressFromLegacy()
    // below, which copies cfg.feedPort into the market.feedserver ingress
    // params that actually bind the socket.
    cfg.feedPort = feedPort;
  };

  opts.hooks.bootFields = [](const Config& cfg, std::vector<Field>& fields) {
    fields.push_back({"feedPort", std::to_string(cfg.feedPort)});
  };

  // ENC-31: ensure cfg.ingress[] is populated even when the user provided
  // no INI (loadFromFile not called). Idempotent.
  //
  // ENC-1331: this is the ONLY caller. It runs here, after the argv overrides
  // above have settled cfg.feedPort, so the synthesized market.feedserver entry
  // carries the port that was actually asked for. Config::loadFromFile() used
  // to call it too, which froze the file's feedPort into the ingress params
  // while the boot log printed argv[3] — the two disagreed and only the bind
  // was true.
  opts.hooks.afterConnectorsRegistered = [](int argc, char* /*argv*/[], Config& cfg) {
    const bool ingressSynthesized = cfg.synthesizeIngressFromLegacy();
    if (argc > 3 && !ingressSynthesized) {
      // The config file declared explicit `ingress.N.*` entries, which are more
      // specific than the legacy feedPort key and are left alone. Say so, rather
      // than silently binding a port the user did not pass.
      logger().log(LogLevel::Warn, "config.feedport_arg_ignored",
                   {{"feedPort", std::to_string(cfg.feedPort)},
                    {"reason", "config file declares explicit ingress.N.* entries"},
                    {"fix", "set ingress.N.port in the config file instead"}});
    }
  };

  opts.hooks.ingressStartFailHint =
      "        (is another krn_server already listening there? "
      "pass a different feedPort as argv[3] / ingress.N.port)";

  // ENC-1331: report the port that was actually RESOLVED into the ingress entry
  // the acceptor was opened from, not the intent in cfg.feedPort. A log that
  // printed the intent is precisely what hid this bug: it showed argv[3] while
  // the socket carried the config file's port, and only the bind was true.
  opts.hooks.listeningFields = [](const Config& cfg, std::vector<Field>& fields) {
    std::string boundFeedPort = "none";
    for (const auto& entry : cfg.ingress) {
      if (entry.kind != "market.feedserver") continue;
      auto it = entry.params.find("port");
      if (it != entry.params.end()) boundFeedPort = it->second;
      break;
    }
    fields.push_back({"feedPort", boundFeedPort});
  };

  return krn::host::hostMain(argc, argv, opts);
}
