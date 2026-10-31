// ENC-1584: non-finite doubles are written as JSON null; finite doubles are
// written byte-for-byte as rapidjson's Writer::Double writes them.
#include "krn/StreamValue.hpp"
#include "krn/util/JsonUtil.hpp"

#include <gtest/gtest.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <cfloat>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace krn;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

// What the engine wrote for a finite double before ENC-1584.
std::string plainDouble(double d) {
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> w(sb);
  EXPECT_TRUE(w.Double(d));
  return std::string(sb.GetString(), sb.GetSize());
}

// The update frame exactly as ClientSession's sendFn builds it.
std::string updateFrame(const ArgType& v) {
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> w(sb);
  w.StartObject();
  w.Key("type");      w.String("update");
  w.Key("key");       w.Int(1);
  w.Key("streamKey"); w.String("REF");
  w.Key("value");     util::writeArgTypeJson(w, v);
  w.EndObject();
  return std::string(sb.GetString(), sb.GetSize());
}

// Strict parse: rapidjson's default flags reject NaN/Infinity literals.
void expectStrictJson(const std::string& s) {
  rapidjson::Document d;
  d.Parse(s.c_str(), s.size());
  EXPECT_FALSE(d.HasParseError()) << s;
}

} // namespace

TEST(JsonUtilNonFiniteTest, NaNIsNull) {
  EXPECT_EQ(util::toJsonString(ArgType{kNaN}), "null");
  EXPECT_EQ(util::toJsonString(ArgType{-kNaN}), "null");
}

TEST(JsonUtilNonFiniteTest, PositiveInfinityIsNull) {
  EXPECT_EQ(util::toJsonString(ArgType{kInf}), "null");
}

TEST(JsonUtilNonFiniteTest, NegativeInfinityIsNull) {
  EXPECT_EQ(util::toJsonString(ArgType{-kInf}), "null");
}

TEST(JsonUtilNonFiniteTest, NegativeZeroIsUnchanged) {
  EXPECT_EQ(util::toJsonString(ArgType{-0.0}), "-0.0");
  EXPECT_EQ(util::toJsonString(ArgType{-0.0}), plainDouble(-0.0));
}

TEST(JsonUtilNonFiniteTest, NormalValueIsUnchanged) {
  EXPECT_EQ(util::toJsonString(ArgType{1.5}), "1.5");
  EXPECT_EQ(util::toJsonString(ArgType{2.0}), "2.0");
  EXPECT_EQ(util::toJsonString(ArgType{0.1}), "0.1");
}

TEST(JsonUtilNonFiniteTest, DblMaxIsUnchanged) {
  EXPECT_EQ(util::toJsonString(ArgType{DBL_MAX}), "1.7976931348623157e308");
  EXPECT_EQ(util::toJsonString(ArgType{-DBL_MAX}), "-1.7976931348623157e308");
}

TEST(JsonUtilNonFiniteTest, VectorDoubleNonFiniteElementsAreNull) {
  ArgType v = std::vector<double>{1.5, kNaN, kInf, -kInf, -0.0, 2.0};
  const std::string s = util::toJsonString(v);
  EXPECT_EQ(s, "[1.5,null,null,null,-0.0,2.0]");
  expectStrictJson(s);
}

TEST(JsonUtilNonFiniteTest, NestedArrayAndRecordNonFiniteAreNull) {
  ArgType arr = std::vector<ArgValue>{ArgValue{ArgType{kNaN}},
                                      ArgValue{ArgType{std::vector<double>{-kInf, 3.0}}},
                                      ArgValue{ArgType{1}}};
  EXPECT_EQ(util::toJsonString(arr), "[null,[null,3.0],1]");

  Record r;
  recordSet(r, "o", 1.5);
  recordSet(r, "h", kInf);
  recordSet(r, "c", kNaN);
  recordSet(r, "nested", ArgType{arr});
  const std::string s = util::toJsonString(ArgType{r});
  EXPECT_EQ(s, R"({"o":1.5,"h":null,"c":null,"nested":[null,[null,3.0],1]})");
  expectStrictJson(s);
}

TEST(JsonUtilNonFiniteTest, UpdateFrameWithNonFiniteValueParses) {
  for (double d : {kNaN, kInf, -kInf}) {
    const std::string s = updateFrame(ArgType{d});
    EXPECT_EQ(s, R"({"type":"update","key":1,"streamKey":"REF","value":null})");
    rapidjson::Document doc;
    doc.Parse(s.c_str(), s.size());
    ASSERT_FALSE(doc.HasParseError()) << s;
    ASSERT_TRUE(doc.HasMember("value"));
    EXPECT_TRUE(doc["value"].IsNull());
  }
}

// Every finite double must serialise exactly as Writer::Double does.
TEST(JsonUtilNonFiniteTest, FiniteDoublesAreByteIdenticalToWriterDouble) {
  std::vector<double> cases = {0.0, -0.0, 1.0, -1.0, 0.1, 1e-7, 1e21, 123456.789,
                               DBL_MAX, -DBL_MAX, DBL_MIN, -DBL_MIN,
                               std::numeric_limits<double>::denorm_min(),
                               -std::numeric_limits<double>::denorm_min(),
                               std::numeric_limits<double>::epsilon()};
  std::mt19937_64 rng(1584);
  while (cases.size() < 200000) {
    const std::uint64_t bits = rng();
    double d;
    std::memcpy(&d, &bits, sizeof d);
    if (std::isfinite(d)) cases.push_back(d);
  }
  std::uniform_real_distribution<double> price(0.0, 1000.0);
  for (int i = 0; i < 100000; ++i) cases.push_back(price(rng));

  std::size_t mismatches = 0;
  for (double d : cases) {
    if (util::toJsonString(ArgType{d}) != plainDouble(d)) ++mismatches;
    if (util::toJsonString(ArgType{std::vector<double>{d}}) != "[" + plainDouble(d) + "]")
      ++mismatches;
  }
  EXPECT_EQ(mismatches, 0u);
}
