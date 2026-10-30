#pragma once
//
// The book-side L1 records, moved out of the engine's include/krn/data/Record.hpp
// (ENC-1550). No engine code names them, so they live in krn_data. RecordHeader,
// TickRecord and Aggressor followed them in ENC-1557, into krn/data/TickRecord.hpp,
// which this header includes.
//
// Design: specs/2026-10-15-gma-engine-purity/SPEC.md D4, D10.

#include <cstddef>
#include <cstdint>

#include "krn/data/TickRecord.hpp"

namespace krn::data {

// Mirrors krn::Side (connectors/market/include/krn/book/OrderBook.hpp).
enum class Side : std::uint8_t { Bid = 0, Ask = 1 };

// Mirrors the krn::feed::FeedEvent variant set
// (connectors/market/include/krn/feed/FeedEvent.hpp) one-for-one. That variant
// is already the canonical event model in this repo; the schema does not invent
// a second one.
enum class BookAction : std::uint8_t {
  Add      = 0,   // ObAddEvent
  Update   = 1,   // ObUpdateEvent
  Delete   = 2,   // ObDeleteEvent
  Trade    = 3,   // ObTradeEvent (print; see bookNeutral)
  TickSize = 4,   // ObTickSizeEvent
  Clear    = 5,   // ObResetEvent — new epoch
};

// One L2/L3 book mutation. Maps onto the OrderBookManager entry points that
// already exist (onAdd / onUpdate / onDelete / onTrade / setTickSize /
// onReset), so the data layer drives that machinery rather than duplicating it
// (docs/data-layer.md §4.6).
struct BookRecord {
  RecordHeader  header;
  BookAction    action   = BookAction::Add;
  Side          side     = Side::Bid;
  std::int64_t  price    = kNoPrice;
  std::uint64_t size     = 0;
  std::uint64_t orderId  = 0;     // 0 == venue supplied none (OrderKey::idMissing)
  std::uint64_t priority = 0;     // recv seq or venue ts, per Order::priority
  std::uint16_t depth    = 0;     // level index for aggregated (L2) feeds
  std::uint32_t epoch    = 0;     // FeedScope::epoch; bumped by Clear
  std::uint16_t flags    = 0;
};

// One level of a book snapshot. Snapshots are persisted on a cadence so a
// replay of an arbitrary window can seek instead of rebuilding from session
// open — with MBO, the alternative is replaying every order event from the
// open, per research run (docs/data-layer.md §8.5).
struct SnapshotLevel {
  Side          side       = Side::Bid;
  std::int64_t  price      = kNoPrice;
  std::uint64_t size       = 0;
  std::uint32_t orderCount = 0;
};

// One ROW of `book_snapshots`: a level, plus the header, plus the resume seq
// the snapshot was taken at (`onSnapshotPerOrder`'s snapshotSeq). A snapshot is
// many rows sharing one snapshotSeq; the level alone is not addressable, which
// is why the row type exists rather than persisting bare SnapshotLevels.
struct SnapshotRecord {
  RecordHeader  header;
  std::uint64_t snapshotSeq = 0;
  SnapshotLevel level;
};

// Layout and enumerator values are part of the L1 contract (Schema.cpp casts
// them; clickhouse.sql stores them). Pinned as measured before the move.
static_assert(sizeof(Side) == 1 && static_cast<int>(Side::Bid) == 0 &&
              static_cast<int>(Side::Ask) == 1);
static_assert(sizeof(BookAction) == 1 &&
              static_cast<int>(BookAction::Add) == 0 &&
              static_cast<int>(BookAction::Update) == 1 &&
              static_cast<int>(BookAction::Delete) == 2 &&
              static_cast<int>(BookAction::Trade) == 3 &&
              static_cast<int>(BookAction::TickSize) == 4 &&
              static_cast<int>(BookAction::Clear) == 5);

static_assert(sizeof(BookRecord) == 96 && alignof(BookRecord) == 8);
static_assert(offsetof(BookRecord, header) == 0 &&
              offsetof(BookRecord, action) == 40 &&
              offsetof(BookRecord, side) == 41 &&
              offsetof(BookRecord, price) == 48 &&
              offsetof(BookRecord, size) == 56 &&
              offsetof(BookRecord, orderId) == 64 &&
              offsetof(BookRecord, priority) == 72 &&
              offsetof(BookRecord, depth) == 80 &&
              offsetof(BookRecord, epoch) == 84 &&
              offsetof(BookRecord, flags) == 88);

static_assert(sizeof(BookRecord::price) == 8 && sizeof(BookRecord::size) == 8 &&
              sizeof(BookRecord::orderId) == 8 &&
              sizeof(BookRecord::priority) == 8 &&
              sizeof(BookRecord::depth) == 2 && sizeof(BookRecord::epoch) == 4 &&
              sizeof(BookRecord::flags) == 2);

static_assert(sizeof(SnapshotLevel) == 32 && alignof(SnapshotLevel) == 8);
static_assert(offsetof(SnapshotLevel, side) == 0 &&
              offsetof(SnapshotLevel, price) == 8 &&
              offsetof(SnapshotLevel, size) == 16 &&
              offsetof(SnapshotLevel, orderCount) == 24);

static_assert(sizeof(SnapshotLevel::price) == 8 &&
              sizeof(SnapshotLevel::size) == 8 &&
              sizeof(SnapshotLevel::orderCount) == 4);

static_assert(sizeof(SnapshotRecord) == 80 && alignof(SnapshotRecord) == 8);
static_assert(offsetof(SnapshotRecord, header) == 0 &&
              offsetof(SnapshotRecord, snapshotSeq) == 40 &&
              offsetof(SnapshotRecord, level) == 48);

} // namespace krn::data
