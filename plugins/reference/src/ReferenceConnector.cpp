#include "krn/reference/ReferenceConnector.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <stdexcept>

#include <boost/asio/io_context.hpp>
#include <rapidjson/document.h>

#include "krn/Dispatcher.hpp"
#include "krn/Event.hpp"
#include "krn/atomic/AtomicProviderRegistry.hpp"
#include "krn/engine/EventComputerRegistry.hpp"
#include "krn/engine/ProtocolIngressRegistry.hpp"
#include "krn/reference/ReferenceComputer.hpp"

namespace krn::synthetic {

ReferenceConnector::ReferenceConnector() = default;
ReferenceConnector::ReferenceConnector(Options opts) : _opts(std::move(opts)) {}
ReferenceConnector::~ReferenceConnector() = default;

void ReferenceConnector::registerProtocols(engine::EngineRegistries& reg) {
  if (!reg.protocols) {
    throw std::runtime_error("ReferenceConnector: EngineRegistries has no protocol registry");
  }
  reg.protocols->registerProtocol(std::string(kProtocol), std::string(kIngressKind));
}

void ReferenceConnector::registerWith(engine::EngineRegistries& reg) {
  if (!reg.dispatcher || !reg.io || !reg.computers || !reg.protocols) {
    throw std::runtime_error("ReferenceConnector: incomplete EngineRegistries");
  }
  reg.computers->registerFactory(std::string(ReferenceComputer::kEventType), [] {
    return std::make_unique<ReferenceComputer>();
  });
  AtomicProviderRegistry::markPipelineOnly(std::string(kPipelineOnlyNamespace));
  registerProtocols(reg);
  _timer = std::make_unique<boost::asio::steady_timer>(*reg.io);
  _dispatcher = reg.dispatcher;
}

void ReferenceConnector::start() {
  if (!_timer || !_dispatcher) return;

  auto* timer      = _timer.get();
  auto  streamKey  = _opts.streamKey;
  auto  period     = std::chrono::milliseconds(std::max(1, _opts.tickMs));
  int   maxTicks   = _opts.maxTicks;
  double base      = _opts.sampleBase;
  auto  counter    = std::make_shared<std::uint64_t>(0);
  auto* dispatcher = _dispatcher;

  _tickHandler = std::make_shared<std::function<void(const boost::system::error_code&)>>();
  std::weak_ptr<std::function<void(const boost::system::error_code&)>> weakSelf = _tickHandler;
  *_tickHandler = [timer, streamKey, period, maxTicks, base, counter, dispatcher, weakSelf]
                  (const boost::system::error_code& ec) {
    if (ec) return;
    if (maxTicks > 0 && *counter >= static_cast<std::uint64_t>(maxTicks)) return;

    auto doc = std::make_shared<rapidjson::Document>();
    doc->SetObject();
    doc->AddMember(rapidjson::StringRef(ReferenceComputer::kInputField.data(),
                                        ReferenceComputer::kInputField.size()),
                   rapidjson::Value(base + static_cast<double>(*counter)),
                   doc->GetAllocator());
    ++*counter;

    Event ev;
    ev.type    = std::string(ReferenceComputer::kEventType);
    ev.symbol  = streamKey;
    ev.payload = std::move(doc);
    dispatcher->onTick(ev);

    if (auto self = weakSelf.lock()) {
      timer->expires_after(period);
      timer->async_wait(*self);
    }
  };

  timer->expires_after(period);
  timer->async_wait(*_tickHandler);
}

void ReferenceConnector::stop() noexcept {
  if (!_timer) return;
  try { _timer->cancel(); } catch (...) {}
  _tickHandler.reset();
}

} // namespace krn::synthetic
