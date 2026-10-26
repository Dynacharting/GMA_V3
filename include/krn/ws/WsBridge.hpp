#pragma once
#include <memory>
#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>

#include <rapidjson/document.h>

#include "krn/Dispatcher.hpp"
#include "krn/AtomicStore.hpp"
#include "krn/rt/ThreadPool.hpp"
#include "krn/nodes/INode.hpp"

namespace krn::ws {

class WsBridge {
public:
  using SendFn = std::function<void(const std::string&)>; // implement with your socket's send-text

  WsBridge(std::shared_ptr<krn::Dispatcher> dispatcher,
           std::shared_ptr<krn::AtomicStore>      store,
           krn::rt::ThreadPool*                    pool = nullptr)
    : dispatcher_(std::move(dispatcher)), store_(std::move(store)), pool_(pool) {}

  // Call these from your WebSocketServer at the right time:
  void onOpen(const std::string& connId, SendFn send);
  void onMessage(const std::string& connId, const std::string& text);
  void onClose(const std::string& connId);

private:
  void sendTo(const std::string& connId, const std::string& msg);
  void handleSubscribe(const std::string& connId, const rapidjson::Document& doc);
  void handleCancel(const std::string& connId, const rapidjson::Document& doc);

  std::mutex mx_;
  std::unordered_map<std::string, SendFn> connections_;

  // Active subscriptions per connection: connId -> (reqId -> head node)
  std::unordered_map<std::string,
    std::unordered_map<std::string, std::shared_ptr<krn::INode>>> active_;

  // Keep intermediate pipeline nodes alive (Listener uses weak_ptr downstream).
  std::unordered_map<std::string,
    std::unordered_map<std::string, std::vector<std::shared_ptr<krn::INode>>>> chains_;

  std::shared_ptr<krn::Dispatcher> dispatcher_;
  std::shared_ptr<krn::AtomicStore>      store_;
  krn::rt::ThreadPool*                    pool_ = nullptr;

  static constexpr std::size_t MAX_SUBS_PER_CONN = 100;
};

} // namespace krn::ws
