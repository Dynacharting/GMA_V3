// Transitional umbrella header. Existing consumers still include this file;
// the split is forward-looking — new code should prefer the concrete headers:
//   - Market TA: krn/MarketTA.hpp   (moves into the market connector in Step 7)
//   - Builtin worker functions: krn/FunctionRegistry.hpp
// The umbrella is removed in Step 11 of the engine/connector refactor.
#pragma once

#include "krn/FunctionRegistry.hpp"
#include "krn/market/MarketTA.hpp"
