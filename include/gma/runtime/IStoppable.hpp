#pragma once
namespace krn::rt {
struct IStoppable {
  virtual ~IStoppable() = default;
  virtual void stop() = 0; // idempotent
};
} // namespace krn::rt
