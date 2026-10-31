#pragma once
#include <string>
#include <unordered_map>
#include <functional>
#include <optional>
#include <mutex>
#include <unordered_set>

namespace krn {

// Minimal, thread-safe registry that lets AtomicAccessor resolve keys by namespace (prefix before first '.')
class AtomicProviderRegistry {
public:
  using ProviderFn = std::function<double(const std::string& symbol, const std::string& fullKey)>;

  // Tag instance — registry methods are static, so this is purely a handle for
  // EngineRegistries to expose alongside the per-instance singletons.
  static AtomicProviderRegistry& singleton() {
    static AtomicProviderRegistry s;
    return s;
  }

  // Register (or replace) a provider function for a namespace prefix.
  static void registerNamespace(const std::string& ns, ProviderFn fn) {
    std::lock_guard<std::mutex> lk(mx());
    map()[ns] = std::move(fn);
  }

  // Remove a namespace; returns true if something was erased
  static bool unregisterNamespace(const std::string& ns) {
    std::lock_guard<std::mutex> lk(mx());
    return map().erase(ns) > 0;
  }

  // Clear all providers (primarily for tests or full reinit)
  static void clear() {
    std::lock_guard<std::mutex> lk(mx());
    map().clear();
  }

  // Try to resolve and evaluate a key of the form "<ns>.<rest>"
  // Returns std::nullopt if no provider is found.
  static std::optional<double> tryResolve(const std::string& symbol, const std::string& key) {
    auto dot = key.find('.');
    if (dot == std::string::npos) return std::nullopt;
    const std::string ns = key.substr(0, dot);

    // Copy the provider function under lock, then call outside lock
    // to avoid blocking all concurrent resolves while one provider executes.
    ProviderFn fn;
    {
      std::lock_guard<std::mutex> lk(mx());
      auto it = map().find(ns);
      if (it == map().end()) return std::nullopt;
      fn = it->second;
    }
    try {
      return fn(symbol, key);
    } catch (...) {
      return std::nullopt;
    }
  }

  // ENC-1575: a namespace a plugin declares PIPELINE-ONLY is written into the
  // AtomicStore but never pushed through Dispatcher::notifyListeners, so a
  // Listener bound to one of its keys would register and never fire. Listener
  // refuses such keys at construction. Independent of registerNamespace(): a
  // namespace may be flagged with or without a provider.
  //
  // `ns` is a whole namespace (no '.'); returns false, and flags nothing, for
  // an empty or dotted one.
  static bool markPipelineOnly(const std::string& ns) {
    if (ns.empty() || ns.find('.') != std::string::npos) return false;
    std::lock_guard<std::mutex> lk(mx());
    pipelineOnly().insert(ns);
    return true;
  }

  static bool unmarkPipelineOnly(const std::string& ns) {
    std::lock_guard<std::mutex> lk(mx());
    return pipelineOnly().erase(ns) > 0;
  }

  // True iff `key` has the form "<ns>.<rest>" and <ns>, the text before the
  // FIRST '.', is exactly a flagged namespace. A key with no '.' is never
  // pipeline-only, so a flagged `ob` refuses `ob.x` but not `obsolete`.
  static bool isPipelineOnlyKey(const std::string& key) {
    auto dot = key.find('.');
    if (dot == std::string::npos) return false;
    std::lock_guard<std::mutex> lk(mx());
    return pipelineOnly().count(key.substr(0, dot)) > 0;
  }

private:
  static std::unordered_set<std::string>& pipelineOnly() {
    static std::unordered_set<std::string> instance;
    return instance;
  }
  static std::unordered_map<std::string, ProviderFn>& map() {
    static std::unordered_map<std::string, ProviderFn> instance;
    return instance;
  }
  static std::mutex& mx() {
    static std::mutex m;
    return m;
  }
};

} // namespace krn
