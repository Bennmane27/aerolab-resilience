// AEROLAB RESILIENCE - minimal JSON value, parser and writer.
//
// Requirements: DATA-001 (UTF-8), DATA-002 (versioned schema), DATA-003 (units
// explicit in field names), DATA-008 (an incompatible schema is refused, never
// silently converted), DATA-009 (enough float precision to replay).
//
// Numbers are written with %.17g, which round-trips an IEEE-754 double exactly.
// Object keys are kept in a std::map, so serialisation order is deterministic
// and two runs of the same configuration produce byte-identical files - which
// is what makes the config hash in the manifest meaningful (DATA-005).
#pragma once

#include <map>
#include <string>
#include <vector>

namespace aerolab {

class Json {
 public:
  enum class Type { kNull, kBool, kNumber, kString, kArray, kObject };

  Json() = default;
  static Json object() {
    Json j;
    j.type_ = Type::kObject;
    return j;
  }
  static Json array() {
    Json j;
    j.type_ = Type::kArray;
    return j;
  }
  Json(bool v) : type_(Type::kBool), bool_(v) {}        // NOLINT(runtime/explicit)
  Json(double v) : type_(Type::kNumber), number_(v) {}  // NOLINT(runtime/explicit)
  Json(int v) : type_(Type::kNumber), number_(v) {}     // NOLINT(runtime/explicit)
  Json(long long v) : type_(Type::kNumber), number_(static_cast<double>(v)) {}           // NOLINT
  Json(unsigned long long v) : type_(Type::kNumber), number_(static_cast<double>(v)) {}  // NOLINT
  Json(const char* v) : type_(Type::kString), string_(v) {}                              // NOLINT
  Json(const std::string& v) : type_(Type::kString), string_(v) {}                       // NOLINT

  Type type() const { return type_; }
  bool isNull() const { return type_ == Type::kNull; }
  bool isObject() const { return type_ == Type::kObject; }
  bool isArray() const { return type_ == Type::kArray; }
  bool isNumber() const { return type_ == Type::kNumber; }
  bool isString() const { return type_ == Type::kString; }
  bool isBool() const { return type_ == Type::kBool; }

  // Accessors. The `has` / `get` pair returns false rather than throwing, so a
  // malformed configuration produces a diagnostic instead of a crash (SYS-010).
  bool has(const std::string& key) const {
    return type_ == Type::kObject && object_.find(key) != object_.end();
  }
  const Json& operator[](const std::string& key) const;
  Json& operator[](const std::string& key);
  const Json& at(std::size_t index) const;
  std::size_t size() const;
  void push(const Json& v);

  bool getDouble(const std::string& key, double& out) const;
  bool getInt(const std::string& key, int& out) const;
  bool getUint64(const std::string& key, unsigned long long& out) const;
  bool getBool(const std::string& key, bool& out) const;
  bool getString(const std::string& key, std::string& out) const;

  double asDouble(double fallback = 0.0) const {
    return type_ == Type::kNumber ? number_ : fallback;
  }
  bool asBool(bool fallback = false) const { return type_ == Type::kBool ? bool_ : fallback; }
  std::string asString(const std::string& fallback = std::string()) const {
    return type_ == Type::kString ? string_ : fallback;
  }
  const std::map<std::string, Json>& objectItems() const { return object_; }
  const std::vector<Json>& arrayItems() const { return array_; }

  std::string dump(int indent = 2) const;

  static bool parse(const std::string& text, Json& out, std::string& error);
  static bool parseFile(const std::string& path, Json& out, std::string& error);
  bool writeFile(const std::string& path, int indent = 2) const;

 private:
  void dumpTo(std::string& out, int indent, int depth) const;

  Type type_{Type::kNull};
  bool bool_{false};
  double number_{0.0};
  std::string string_;
  std::vector<Json> array_;
  std::map<std::string, Json> object_;
};

// Encodes a double for JSON output. NaN and Inf are written as null, because a
// bare NaN token is not valid JSON and silently emitting one would produce
// files that some readers accept and others reject (DATA-008).
std::string jsonNumber(double v);

}  // namespace aerolab
