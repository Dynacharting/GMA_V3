#include "krn/nodes/Responder.hpp"
#include "krn/util/Logger.hpp"

using namespace krn::nodes;

Responder::Responder(SendFn send, krn::server::RequestKey key)
  : send_(std::move(send))
  , key_(std::move(key))
{}

void Responder::onValue(const StreamValue& sv) {
    // Early-out on stopped_ is an optimization; correctness is guaranteed by
    // the mutex: if shutdown() races, the lock ensures we see send_==nullptr.
    if (stopped_.load(std::memory_order_acquire)) return;

    // Copy send_ under lock to avoid TOCTOU race with shutdown().
    SendFn fn;
    {
        std::lock_guard<std::mutex> lk(mx_);
        fn = send_;
    }
    if (!fn) return;

    try {
        fn(key_, sv);
    } catch (const std::exception& ex) {
        krn::util::logger().log(krn::util::LogLevel::Error,
                                "Responder send failed",
                                { {"err", ex.what()} });
    }
}

void Responder::shutdown() noexcept {
    stopped_.store(true, std::memory_order_release);
    std::lock_guard<std::mutex> lk(mx_);
    send_ = nullptr;
}
