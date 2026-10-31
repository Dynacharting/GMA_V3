#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "krn/util/Logger.hpp"

namespace krn::util {
class Config;
}
namespace krn::engine {
class IConnector;
}

// krn_host — the engine's composition, shared by every server binary
// (ENC-1559, purity SPEC D6). It loads the config, registers the builtins,
// builds the ThreadPool, AtomicStore, Dispatcher and WebSocket server, hands
// the EngineRegistries to the caller's connectors, pulls ingress from forum,
// instantiates and starts the configured ingress sources, and runs until
// SIGINT/SIGTERM through the ShutdownCoordinator.
//
// It names no plugin. A binary supplies its connectors and, through HostHooks,
// the CLI arguments and log fields that belong to them.
//
// CLI: argv[1] = wsPort, argv[2] = config file. argv[3] onward belong to the
// caller (HostHooks::applyExtraArgs).
namespace krn::host {

struct HostHooks {
  // After the config file is loaded and wsPort is resolved, before the "boot"
  // log line. Applies the caller's own arguments to cfg. A hook that reads
  // keys from the config file itself calls cfg.markConsumed() for each one,
  // or the host warns that nothing claimed it (ENC-1595).
  std::function<void(int argc, char* argv[], util::Config& cfg)> applyExtraArgs;

  // Appended, in order, after wsPort on the "boot" log line.
  std::function<void(const util::Config& cfg, std::vector<util::Field>& fields)> bootFields;

  // After every connector's registerWith(), before the forum ingress pull and
  // before cfg.dispatchPendingKeys().
  std::function<void(int argc, char* argv[], util::Config& cfg)> afterConnectorsRegistered;

  // Printed on stderr, after "[fatal] cannot start ingress '<kind>': <error>",
  // when an ingress source fails to start. Empty prints nothing more.
  std::string ingressStartFailHint;

  // Appended, in order, after wsPort on the "listening" log line.
  std::function<void(const util::Config& cfg, std::vector<util::Field>& fields)> listeningFields;
};

// Builds one connector. Called inside runHost(), so the connector is
// constructed after the io_context exists and destroyed before it is.
using ConnectorFactory = std::function<std::unique_ptr<engine::IConnector>()>;

struct HostOptions {
  // Used in diagnostics ("is another <programName> already listening there?").
  std::string programName = "krn";
  // Each is constructed and registerWith()'d in this order, start()ed in this
  // order once ingress is up, and stop()ped in reverse.
  std::vector<ConnectorFactory> connectors;
  HostHooks hooks;
};

// Runs the server to completion. Returns EXIT_SUCCESS after a clean shutdown,
// EXIT_FAILURE when a listening socket cannot be opened.
int runHost(int argc, char* argv[], const HostOptions& opts);

// runHost() behind a catch: an escaping std::exception is reported as
// "[fatal] <programName>: <what>" on stderr and returns EXIT_FAILURE.
int hostMain(int argc, char* argv[], const HostOptions& opts);

} // namespace krn::host
