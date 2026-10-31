#pragma once
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace krn::engine {

// Maps a forum connector row's `protocol` to the ingress kind that serves it
// (ENC-1574). A plugin registers the pair in registerWith(); the engine's
// forum client looks the protocol up and builds an ingress entry of that kind.
// A protocol nobody registered has no kind: the row is logged and skipped,
// never given a default.
class ProtocolIngressRegistry {
public:
  // Tag instance — see EventTypeRegistry::singleton() for rationale.
  static ProtocolIngressRegistry& singleton() {
    static ProtocolIngressRegistry s;
    return s;
  }

  // First registration wins; a second one for the same protocol returns false.
  static bool registerProtocol(std::string protocol, std::string ingressKind) {
    std::lock_guard lk(mx());
    auto [it, ok] = map().emplace(std::move(protocol), std::move(ingressKind));
    return ok;
  }

  static std::optional<std::string> find(std::string_view protocol) {
    std::lock_guard lk(mx());
    auto it = map().find(std::string(protocol));
    if (it == map().end()) return std::nullopt;
    return it->second;
  }

  static bool contains(std::string_view protocol) {
    std::lock_guard lk(mx());
    return map().count(std::string(protocol)) > 0;
  }

  static std::vector<std::string> protocols() {
    std::lock_guard lk(mx());
    std::vector<std::string> out;
    out.reserve(map().size());
    for (auto& kv : map()) out.push_back(kv.first);
    return out;
  }

  static void clear() {
    std::lock_guard lk(mx());
    map().clear();
  }

private:
  static std::unordered_map<std::string, std::string>& map() {
    static std::unordered_map<std::string, std::string> m;
    return m;
  }
  static std::mutex& mx() {
    static std::mutex m;
    return m;
  }
};

} // namespace krn::engine
