// Check E's positive control (ENC-1579).  Never compiled.
// Each identifier in code is derived market vocabulary, two per source, and
// must hit.  `mean` is exempt and must not.  A line marked DECOY holds the
// words only in a comment or a literal, so a hit there is a stripper leak.
class MarketTickComputer;          // type
struct ObKey;                      // type
std::vector<int> priceFields;      // fieldmap
bool taEnabled;                    // fieldmap
int imbalance;                     // obkey
int notional;                      // obkey
int kBidSize;                      // k_literal
int kAskSize;                      // k_literal
int lastPrice;                     // atomic
int sma_20;                        // atomic, prefix sma_
int mean;                          // exempt builtin
const char* d1 = "MarketFieldMap OrderBook vwap kPrice";  // DECOY
/* MarketConnector ObTradeEvent bidFields */ int d2;      // DECOY
int d3; // ItchAdapter rsi_14 openPrice                      DECOY
