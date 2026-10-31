#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>

#include "krn/engine/IConnector.hpp"

namespace krn {
class Dispatcher;
}

namespace krn::synthetic {

// Drives ReferenceComputer in a running server (ENC-1559): every tickMs it
// emits a `ReferenceComputer::kEventType` event on `streamKey` whose
// `ReferenceComputer::kInputField` is sampleBase + n for the n-th tick, and it
// registers the ReferenceComputer factory. With sampleBase > 1 every input is
// > 1, so each post-warm-up `reference.sum` / `reference.sumsq` is distinct.
class ReferenceConnector final : public engine::IConnector {
public:
  struct Options {
    std::string streamKey  = "REF";
    int         tickMs     = 100;
    int         maxTicks   = 0;     // 0 = unbounded
    double      sampleBase = 100.0;
  };

  ReferenceConnector();
  explicit ReferenceConnector(Options opts);
  ~ReferenceConnector() override;

  // ENC-1575: the namespace of ReferenceComputer's keys. They are written into
  // the store and never pushed, so registerWith() declares the namespace
  // pipeline-only and a Listener on `reference.sum` is refused.
  static constexpr std::string_view kPipelineOnlyNamespace = "reference";

  // The forum connector protocol this plugin registers, and the ingress kind
  // it maps to (ENC-1574). No ingress factory is registered for the kind.
  // registerWith() calls registerProtocols(); tests call it alone, because
  // registerWith() also appends a computer factory to a process-wide registry.
  static constexpr std::string_view kProtocol    = "reference";
  static constexpr std::string_view kIngressKind = "reference.ingress";
  static void registerProtocols(engine::EngineRegistries& reg);

  std::string_view name() const override { return "reference"; }
  void registerWith(engine::EngineRegistries& reg) override;
  void start() override;
  void stop() noexcept override;

private:
  Options                                    _opts;
  std::unique_ptr<boost::asio::steady_timer> _timer;
  Dispatcher*                                _dispatcher { nullptr };
  // Same ownership as SyntheticConnector (L4/ENC-805): the handler captures a
  // weak_ptr to itself, so there is no self-referential cycle.
  std::shared_ptr<std::function<void(const boost::system::error_code&)>> _tickHandler;
};

} // namespace krn::synthetic
