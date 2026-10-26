#include "krn/engine/IEventComputer.hpp"

#include "krn/Event.hpp"
#include "krn/engine/JsonFieldSource.hpp"

namespace krn::engine {

// The JSON DOM adapter. Lives in a .cpp so `IEventComputer.hpp` — which every
// connector includes — stays free of rapidjson.
void IEventComputer::computeEvent(const Event& e, ComputeContext& ctx) {
  // A null payload is an EMPTY field set, not a skipped call — see the
  // null-accepting JsonFieldSource ctor for why that distinction matters.
  const JsonFieldSource fields(e.payload.get());
  compute(e.symbol, fields, ctx);
}

} // namespace krn::engine
