#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include "krn/nodes/INode.hpp"

namespace krn::ws {

// Sends JSON {type:"update", id, symbol, value, ts} via provided sendText()
class WSResponder : public INode {
public:
  using SendFn = std::function<void(const std::string& jsonText)>;

  WSResponder(std::string reqId, SendFn send)
  : reqId_(std::move(reqId)), send_(std::move(send)) {}

  void onValue(const StreamValue& v) override;
  void shutdown() noexcept override;

private:
  std::string reqId_;
  std::atomic<bool> stopped_{false};
  std::mutex mx_;
  SendFn send_;
};

} // namespace krn::ws
