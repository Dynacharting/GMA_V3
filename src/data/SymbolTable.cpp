#include "krn/engine/SymbolTable.hpp"

#include <mutex>

namespace krn::data {

SymbolId SymbolTable::intern(std::string_view text) {
  // Fast path: already present. Taken under a shared lock so concurrent
  // decoders of the same symbol do not serialize on the exclusive one.
  {
    std::shared_lock lk(mu_);
    auto it = ids_.find(std::string(text));
    if (it != ids_.end()) return it->second;
  }

  std::unique_lock lk(mu_);
  // Re-check: another thread may have interned it between the two locks.
  auto it = ids_.find(std::string(text));
  if (it != ids_.end()) return it->second;

  names_.emplace_back(text);
  const auto id = static_cast<SymbolId>(names_.size());  // 1-based; 0 reserved
  ids_.emplace(names_.back(), id);
  return id;
}

SymbolId SymbolTable::lookup(std::string_view text) const {
  std::shared_lock lk(mu_);
  auto it = ids_.find(std::string(text));
  return it == ids_.end() ? kInvalidSymbolId : it->second;
}

std::string SymbolTable::name(SymbolId id) const {
  std::shared_lock lk(mu_);
  if (id == kInvalidSymbolId || id > names_.size()) return {};
  return names_[id - 1];
}

bool SymbolTable::contains(SymbolId id) const {
  std::shared_lock lk(mu_);
  return id != kInvalidSymbolId && id <= names_.size();
}

std::size_t SymbolTable::size() const {
  std::shared_lock lk(mu_);
  return names_.size();
}

void SymbolTable::clear() {
  std::unique_lock lk(mu_);
  names_.clear();
  ids_.clear();
}

} // namespace krn::data
