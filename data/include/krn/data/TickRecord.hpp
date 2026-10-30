#pragma once
//
// Canonical engine-facing market-data records (ENC-1481).
//
// Design: specs/2026-10-14-krn-data-layer/SPEC.md D3, D8;
//         docs/data-layer.md §5.3, §8.3.
//
// WHERE THIS FILE LIVES
// ---------------------
// It was `include/krn/data/Record.hpp`, inside `krn_engine`, while
// `Dispatcher::onRecord` took a `TickRecord`. Since ENC-1557 `onRecord` takes
// an `engine::FieldSource`, so no engine code names a record and the PODs
// live in `krn_data` (purity SPEC D4, D10). The adapter from a record to the
// engine's field view is `krn/data/RecordFieldSource.hpp`, beside this file.
// The PODs stay Arrow-free and dependency-free — <cstdint>/<cstddef> only;
// `krn/data/Schema.hpp` is where Arrow is linked and where it stops.
//
// PRICES ARE int64 AT A FIXED GLOBAL SCALE (kPriceScale, 1e-9), matching
// Databento DBN exactly (SPEC D8). NOT per-symbol: `OrderBookManager`'s
// per-symbol tick size (setTickSize, default 1e-4) stays a book-quantization
// concern and does not reach the schema. Consequence: no per-row scale column,
// no per-symbol lookup on decode, and an exact integer path from DBN with no
// double round trip.
//
// SYMBOLS ARE INTERNED uint32 IDS, NOT STRINGS (SPEC D3). `StreamValue`
// carries `std::string symbol`, so today a symbol is heap-copied into every
// value on every edge of every DAG. For a chart tick that is irrelevant; at
// MBO rates (tens of millions of records per symbol-day) that single copy is a
// measurable fraction of total cost — which is what ENC-1487 (B2) measures.
// Resolve an id back to text only at an output boundary, through
// `krn::data::SymbolTable` (include/krn/data/SymbolTable.hpp, in krn_engine).
//

#include <cstddef>
#include <cstdint>

namespace krn::data {

// ---------------------------------------------------------------------------
// Fixed point
// ---------------------------------------------------------------------------

// Price scale denominator. A price of 1.234567890 is stored as 1234567890.
// Compile-time constant on purpose — see the header comment.
inline constexpr std::int64_t kPriceScale = 1'000'000'000LL;  // 1e-9

// Sentinel for "no price" (an empty book side, a quote with one side only).
// INT64_MIN rather than 0, because 0 is a legal price and negative prices are
// legal too (OrderBookManager::setAllowNegativePrices exists for bonds).
inline constexpr std::int64_t kNoPrice = INT64_MIN;

constexpr double toDouble(std::int64_t fixed) noexcept {
  return static_cast<double>(fixed) / static_cast<double>(kPriceScale);
}

// Rounds half away from zero. The normalizer must prefer an exact integer path
// from the provider's own fixed point (DBN is already 1e-9) and use this only
// for a provider that hands over a double.
std::int64_t toFixed(double px) noexcept;

// ---------------------------------------------------------------------------
// Enumerations — these MIRROR existing engine/connector enums rather than
// replacing them, so the decode is a cast and not a lookup table.
// ---------------------------------------------------------------------------

// Mirrors krn::Aggressor (connectors/market/include/krn/book/OrderBook.hpp).
enum class Aggressor : std::uint8_t { Unknown = 0, Buy = 1, Sell = 2 };

enum class RecordKind : std::uint8_t {
  Trade        = 0,
  Quote        = 1,   // L1 top-of-book
  BookEvent    = 2,   // L2/L3 delta
  BookSnapshot = 3,
};

// ---------------------------------------------------------------------------
// Engine-facing PODs
// ---------------------------------------------------------------------------

// Columns shared by every record, as the engine sees them. Mirrors
// docs/data-layer.md §8.3's "common columns" one-for-one, so a row of any L1
// table decodes into (header, payload) with nothing left over.
struct RecordHeader {
  // Venue event time. THE ONLY TIME AN OPERATOR MAY KEY OFF (SPEC D9).
  std::int64_t  tsEventNs   = 0;
  // Normalizer receipt. DIAGNOSTICS AND LATENCY ONLY — never an operator
  // input, never a window boundary (docs/data-layer.md §8.3).
  std::int64_t  tsRecvNs    = 0;
  // Monotonic within (sessionId, symbolId); synthesized by the normalizer when
  // the provider has none. Non-null is the point: the splice's dedup predicate
  // has no "what if absent" branch.
  std::uint64_t providerSeq = 0;
  // Normalizer session. Makes providerSeq comparable across a restart; the
  // dedup key is the PAIR (sessionId, providerSeq).
  std::uint32_t sessionId   = 0;

  // Interned ids, not strings — see the header comment. 0 is
  // kInvalidSymbolId: a default-constructed header names no symbol and no
  // venue, and can never be mistaken for a real one.
  std::uint32_t symbolId    = 0;
  std::uint32_t venueId     = 0;

  // The DBN instrument_id, retained for provenance (SPEC D7). It is
  // dataset-scoped and reassignable, so it is NOT a key and NOT an operator
  // input; `gma.symbology` holds the mapping. Carried here because it is free
  // — without it this struct is 36 bytes padded to 40 — and because dropping
  // it would make the Arrow round trip lossy.
  std::uint32_t instrumentId = 0;
};

// The provenance field above is free only if that stays true.
static_assert(sizeof(RecordHeader) == 40,
              "RecordHeader grew: instrumentId was justified as padding-free");

// A trade or an L1 quote, flattened. One struct for both because the engine
// consumes them identically (both update AtomicStore scalars and both clock
// listeners); `kind` says which fields are meaningful.
//
// Replaces, on the Arrow path, the krn::Event{symbol, shared_ptr<Document>,
// type} the JSON path uses. Field resolution happened in the normalizer, so
// there is no MarketFieldMap lookup and no HasMember() probe per tick.
struct TickRecord {
  RecordHeader header;
  RecordKind   kind = RecordKind::Trade;

  // kind == Trade
  std::int64_t  price       = kNoPrice;
  std::uint64_t size        = 0;
  Aggressor     aggressor   = Aggressor::Unknown;
  // Mirrors ObTradeEvent::bookNeutral: a print that must NOT consume resting
  // liquidity (hidden / non-displayable executions, and executions whose book
  // effect already arrived as an explicit add/update/delete). Dropping this
  // double-counts volume against the book.
  bool          bookNeutral = false;
  std::uint64_t tradeId     = 0;

  // kind == Quote. NOTE: `quotes` is not populated in the first slice — there
  // is no mbp-1 in the Databento plan, so top-of-book is computed from the
  // reconstructed book (docs/data-layer.md §8.4). The shape is defined so
  // feed-simulator and future L1-only providers have somewhere to land.
  std::int64_t  bidPx = kNoPrice;
  std::int64_t  askPx = kNoPrice;
  std::uint64_t bidSz = 0;
  std::uint64_t askSz = 0;
  std::uint32_t bidCt = 0;        // order count at the level, when provided
  std::uint32_t askCt = 0;

  std::uint16_t flags = 0;        // provider condition flags, passed through
};

// Layout, pinned (ENC-1557): the move out of the engine must not change one
// byte, and these are what the Arrow codec and the DBN decode assume.
static_assert(sizeof(Aggressor) == 1 && static_cast<int>(Aggressor::Unknown) == 0 &&
              static_cast<int>(Aggressor::Buy) == 1 &&
              static_cast<int>(Aggressor::Sell) == 2);
static_assert(sizeof(RecordKind) == 1 && static_cast<int>(RecordKind::Trade) == 0 &&
              static_cast<int>(RecordKind::Quote) == 1 &&
              static_cast<int>(RecordKind::BookEvent) == 2 &&
              static_cast<int>(RecordKind::BookSnapshot) == 3);
static_assert(alignof(RecordHeader) == 8);
static_assert(offsetof(RecordHeader, tsEventNs) == 0 &&
              offsetof(RecordHeader, tsRecvNs) == 8 &&
              offsetof(RecordHeader, providerSeq) == 16 &&
              offsetof(RecordHeader, sessionId) == 24 &&
              offsetof(RecordHeader, symbolId) == 28 &&
              offsetof(RecordHeader, venueId) == 32 &&
              offsetof(RecordHeader, instrumentId) == 36);
static_assert(sizeof(TickRecord) == 128 && alignof(TickRecord) == 8);
static_assert(offsetof(TickRecord, header) == 0 && offsetof(TickRecord, kind) == 40 &&
              offsetof(TickRecord, price) == 48 && offsetof(TickRecord, size) == 56 &&
              offsetof(TickRecord, aggressor) == 64 &&
              offsetof(TickRecord, bookNeutral) == 65 &&
              offsetof(TickRecord, tradeId) == 72 && offsetof(TickRecord, bidPx) == 80 &&
              offsetof(TickRecord, askPx) == 88 && offsetof(TickRecord, bidSz) == 96 &&
              offsetof(TickRecord, askSz) == 104 && offsetof(TickRecord, bidCt) == 112 &&
              offsetof(TickRecord, askCt) == 116 && offsetof(TickRecord, flags) == 120);
static_assert(sizeof(TickRecord::price) == 8 && sizeof(TickRecord::size) == 8 &&
              sizeof(TickRecord::tradeId) == 8 && sizeof(TickRecord::bidPx) == 8 &&
              sizeof(TickRecord::askPx) == 8 && sizeof(TickRecord::bidSz) == 8 &&
              sizeof(TickRecord::askSz) == 8 && sizeof(TickRecord::bidCt) == 4 &&
              sizeof(TickRecord::askCt) == 4 && sizeof(TickRecord::flags) == 2 &&
              sizeof(TickRecord::bookNeutral) == 1);

} // namespace krn::data
