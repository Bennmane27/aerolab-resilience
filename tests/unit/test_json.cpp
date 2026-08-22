// AEROLAB RESILIENCE - the JSON reader and writer.
//
// This parser exists because the manifests, the evaluation configuration and the
// acceptance blocks all go through it, and a silent misread of a configuration
// file changes published numbers without changing anything visible. SYS-010
// requires a diagnostic rather than a crash on malformed input, which means the
// REJECTION paths carry as much weight as the happy ones and had almost no
// coverage.
//
// The round trip is the property worth having: anything this writer emits, this
// reader must read back identically. A manifest that cannot be re-read is a
// manifest that cannot be audited.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

#include "aerolab/io/json.hpp"

using namespace aerolab;

namespace {

Json parseOrFail(const std::string& text) {
  Json j;
  std::string error;
  EXPECT_TRUE(Json::parse(text, j, error)) << text << " -> " << error;
  return j;
}

void expectRejected(const std::string& text, const char* why) {
  Json j;
  std::string error;
  EXPECT_FALSE(Json::parse(text, j, error)) << why << ": '" << text << "' was accepted";
  EXPECT_FALSE(error.empty()) << "a rejection must say why";
}

/// A scratch path under the system temporary directory.
///
/// Not std::tmpnam: it is a link-time warning on glibc, and this project holds a
/// zero-warning build. The name is deterministic because the tests run one at a
/// time and each removes its own file.
std::string tempPath(const char* stem) {
  return (std::filesystem::temp_directory_path() / ("aerolab_test" + std::string(stem) + ".json"))
      .string();
}

}  // namespace

TEST(Json, ReadsTheLiteralsAndDistinguishesThem) {
  EXPECT_TRUE(parseOrFail("true").isBool());
  EXPECT_TRUE(parseOrFail("true").asBool());
  EXPECT_TRUE(parseOrFail("false").isBool());
  EXPECT_FALSE(parseOrFail("false").asBool(true));
  EXPECT_TRUE(parseOrFail("null").isNull());

  // A literal is not a string that happens to spell it.
  EXPECT_TRUE(parseOrFail("\"true\"").isString());
  EXPECT_EQ(parseOrFail("\"true\"").asString(), "true");

  // asBool / asDouble / asString fall back rather than reinterpreting.
  EXPECT_EQ(parseOrFail("null").asDouble(-1.0), -1.0);
  EXPECT_EQ(parseOrFail("42").asString("none"), "none");
  EXPECT_TRUE(parseOrFail("\"x\"").asBool(true));
}

TEST(Json, SkipsWhitespaceAndLineComments) {
  // The evaluation configuration is meant to be read by a person, so the reader
  // tolerates // comments. They must not survive into the parsed value.
  const std::string text =
      "// leading comment\n"
      "{\n"
      "  \"seeds\": 1000,   // how many per scenario\n"
      "  \"frozen\": true\n"
      "}\n";
  const Json j = parseOrFail(text);
  ASSERT_TRUE(j.isObject());
  EXPECT_EQ(j.size(), 2u);
  double seeds = 0;
  ASSERT_TRUE(j.getDouble("seeds", seeds));
  EXPECT_EQ(seeds, 1000.0);
  bool frozen = false;
  ASSERT_TRUE(j.getBool("frozen", frozen));
  EXPECT_TRUE(frozen);
}

TEST(Json, DecodesEveryStringEscape) {
  const Json j = parseOrFail(R"("a\nb\tc\rd\be\ff\"g\\h\/i")");
  EXPECT_EQ(j.asString(), "a\nb\tc\rd\be\ff\"g\\h/i");
}

TEST(Json, DecodesUnicodeEscapesAcrossTheUtf8Boundaries) {
  // One byte, two bytes and three bytes: the three branches of the encoder.
  EXPECT_EQ(parseOrFail(R"("A")").asString(), "A");
  EXPECT_EQ(parseOrFail(R"("é")").asString(), "\xC3\xA9");      // é
  EXPECT_EQ(parseOrFail(R"("€")").asString(), "\xE2\x82\xAC");  // €
}

TEST(Json, RejectsMalformedInputWithADiagnostic) {
  // SYS-010: a malformed configuration produces a message, never a crash.
  expectRejected("", "empty input");
  expectRejected("{", "unterminated object");
  expectRejected("[1, 2", "unterminated array");
  expectRejected("\"unterminated", "unterminated string");
  expectRejected(R"("bad escape \q")", "unknown escape");
  expectRejected(R"("truncated \u12")", "truncated unicode escape");
  expectRejected("\"trailing backslash \\", "unterminated escape");
  expectRejected("{\"k\" 1}", "missing colon");
  expectRejected("{1: 2}", "non-string key");
  expectRejected("tru", "truncated literal");
  expectRejected("{} garbage", "trailing content after the value");
}

TEST(Json, AccessorsReportMissingAndMistypedFieldsInsteadOfGuessing) {
  const Json j = parseOrFail(
      R"({"n": 3.5, "i": 7, "big": 4294967296, "b": false, "s": "text", "a": [1,2,3]})");

  double d = 0;
  int i = 0;
  unsigned long long u = 0;
  bool b = true;
  std::string s;

  EXPECT_TRUE(j.getDouble("n", d));
  EXPECT_DOUBLE_EQ(d, 3.5);
  EXPECT_TRUE(j.getInt("i", i));
  EXPECT_EQ(i, 7);
  EXPECT_TRUE(j.getUint64("big", u));
  EXPECT_EQ(u, 4294967296ULL);
  EXPECT_TRUE(j.getBool("b", b));
  EXPECT_FALSE(b);
  EXPECT_TRUE(j.getString("s", s));
  EXPECT_EQ(s, "text");

  // Absent, and present but of the wrong type, both fail rather than coerce.
  EXPECT_FALSE(j.getDouble("absent", d));
  EXPECT_FALSE(j.getString("n", s));
  EXPECT_FALSE(j.getBool("s", b));
  EXPECT_FALSE(j.getDouble("s", d));

  EXPECT_TRUE(j.has("n"));
  EXPECT_FALSE(j.has("absent"));
  EXPECT_EQ(j["a"].size(), 3u);
  EXPECT_DOUBLE_EQ(j["a"].at(1).asDouble(), 2.0);

  // Reading past the end, or indexing a non-array, yields null rather than
  // undefined behaviour.
  EXPECT_TRUE(j["a"].at(99).isNull());
  EXPECT_TRUE(j["absent"].isNull());
  EXPECT_EQ(j["n"].size(), 0u);
}

TEST(Json, WritesWhatItCanReadBack) {
  Json root = Json::object();
  root["name"] = Json("a \"quoted\" \\ line\nwith\ttabs");
  root["control"] = Json(std::string("bell:\x07"));  // below 0x20: \u escape
  root["count"] = Json(42);
  root["ratio"] = Json(0.125);
  root["flag"] = Json(true);
  root["nothing"] = Json();
  root["empty_array"] = Json::array();
  root["empty_object"] = Json::object();

  Json list = Json::array();
  list.push(Json(1));
  list.push(Json("two"));
  Json nested = Json::object();
  nested["deep"] = Json(true);
  list.push(nested);
  root["list"] = list;

  for (int indent : {0, 2}) {
    const std::string text = root.dump(indent);
    Json back;
    std::string error;
    ASSERT_TRUE(Json::parse(text, back, error)) << "indent " << indent << ": " << error;
    EXPECT_EQ(back.dump(2), root.dump(2)) << "round trip changed the document";
  }
}

TEST(Json, NonFiniteNumbersAreWrittenAsNull) {
  // DATA-008: a bare NaN token is not valid JSON. Emitting one produces files
  // that some readers accept and others reject, which is worse than losing the
  // value, because the disagreement shows up much later.
  EXPECT_EQ(jsonNumber(std::nan("")), "null");
  EXPECT_EQ(jsonNumber(std::numeric_limits<double>::infinity()), "null");
  EXPECT_EQ(jsonNumber(-std::numeric_limits<double>::infinity()), "null");

  Json j = Json::object();
  j["bad"] = Json(std::nan(""));
  Json back;
  std::string error;
  ASSERT_TRUE(Json::parse(j.dump(), back, error)) << error;
  EXPECT_TRUE(back["bad"].isNull());
}

TEST(Json, FileReadingReportsWhichFileFailed) {
  Json j;
  std::string error;
  EXPECT_FALSE(Json::parseFile("/definitely/not/a/path/aerolab.json", j, error));
  EXPECT_NE(error.find("cannot open"), std::string::npos) << error;

  const std::string path = tempPath("_broken");
  {
    std::ofstream f(path, std::ios::binary);
    f << "{ \"unterminated\": ";
  }
  error.clear();
  EXPECT_FALSE(Json::parseFile(path, j, error));
  // The diagnostic names the file: a campaign reads fourteen of them and
  // "unexpected end of input" on its own says nothing about which.
  EXPECT_NE(error.find(path), std::string::npos) << error;
  std::remove(path.c_str());
}

TEST(Json, WritesAndReadsBackThroughTheFilesystem) {
  Json root = Json::object();
  root["scenario"] = Json("SCN-003");
  root["seed"] = Json(103);
  root["error_m"] = Json(2.5);

  const std::string path = tempPath("_manifest");
  ASSERT_TRUE(root.writeFile(path, 2));

  Json back;
  std::string error;
  ASSERT_TRUE(Json::parseFile(path, back, error)) << error;
  EXPECT_EQ(back.dump(2), root.dump(2));
  std::remove(path.c_str());

  // A path that cannot be opened is reported rather than silently dropped: a
  // run whose manifest was never written must not look like one that succeeded.
  EXPECT_FALSE(root.writeFile("/definitely/not/a/path/out.json", 2));
}
