#pragma once
#include "krn/ob/ObSnapshot.hpp"
#include "krn/ob/ObKey.hpp"

namespace krn::ob {

// Core dispatcher
double eval(const Snapshot& s, const ObKey& k);

// Helpers
double bestPrice(const Snapshot&, Side);
double bestSize(const Snapshot&, Side);
double spread(const Snapshot&);
double mid(const Snapshot&);
double levelIdx(const Snapshot&, const LevelIdx&);
double levelPx(const Snapshot&, const LevelPx&, double tick);
double rangeIdxReduce(const Snapshot&, const RangeIdxSpec&);
double rangePxReduce(const Snapshot&, const RangePxSpec&, double tick);
double cumLevels(const Snapshot&, Side, int N, Target);
double vwapLevels(const Snapshot&, Side, Range);
double vwapPriceBand(const Snapshot&, Side, double p1, double p2, double tick);
double imbalanceLevels(const Snapshot&, Range);
double imbalanceBand(const Snapshot&, double p1, double p2, double tick);
double meta(const Snapshot&, const std::string& field);

} // namespace krn::ob
