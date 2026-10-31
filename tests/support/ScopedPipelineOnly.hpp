#pragma once

// ENC-1575: declares a namespace pipeline-only for the lifetime of one test,
// the way a plugin's registerWith() does, and removes the flag afterwards so
// no other test in the binary sees it. krn has no market plugin, so the `ob`
// tests use this to stand in for its declaration.

#include "krn/atomic/AtomicProviderRegistry.hpp"

#include <string>

namespace krn::testsupport {

class ScopedPipelineOnly {
public:
  explicit ScopedPipelineOnly(std::string ns) : ns_(std::move(ns)) {
    marked_ = krn::AtomicProviderRegistry::markPipelineOnly(ns_);
  }
  ~ScopedPipelineOnly() { krn::AtomicProviderRegistry::unmarkPipelineOnly(ns_); }
  ScopedPipelineOnly(const ScopedPipelineOnly&) = delete;
  ScopedPipelineOnly& operator=(const ScopedPipelineOnly&) = delete;

  bool marked() const { return marked_; }

private:
  std::string ns_;
  bool        marked_ { false };
};

} // namespace krn::testsupport
