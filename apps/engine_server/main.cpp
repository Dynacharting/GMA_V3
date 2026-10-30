// File: apps/engine_server/main.cpp — krn_engine_server: krn_host + the
// reference plugin (ENC-1559, purity SPEC D6/D7). The connector-free server:
// it loads nothing a registry was not given here.
//
// CLI: krn_engine_server [wsPort] [configFile]
//
// Streams it serves: "REF" carries reference.tick events (field `sample`,
// computed keys reference.sum / reference.sumsq), and "SYN" carries
// synthetic.tick events (field `counter`, computed keys synthetic.sin / .cos).
#include <memory>

#include "krn/host/Host.hpp"
#include "krn/reference/ReferenceConnector.hpp"
#include "krn/reference/SyntheticConnector.hpp"

int main(int argc, char* argv[]) {
  krn::host::HostOptions opts;
  opts.programName = "krn_engine_server";
  opts.connectors.push_back([] { return std::make_unique<krn::synthetic::SyntheticConnector>(); });
  return krn::host::hostMain(argc, argv, opts);
}
