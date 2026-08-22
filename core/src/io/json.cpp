#include "aerolab/io/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace aerolab {
namespace {

const Json& nullJson() {
  static const Json kNull;
  return kNull;
}

struct Parser {
  const std::string& s;
  std::size_t i{0};
  std::string error;

  explicit Parser(const std::string& text) : s(text) {}

  void skip() {
    while (i < s.size()) {
      const char c = s[i];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++i;
      } else if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
        while (i < s.size() && s[i] != '\n') ++i;
      } else {
        break;
      }
    }
  }

  bool fail(const std::string& msg) {
    if (error.empty()) {
      std::ostringstream ss;
      ss << msg << " at offset " << i;
      error = ss.str();
    }
    return false;
  }

  bool parseValue(Json& out) {
    skip();
    if (i >= s.size()) return fail("unexpected end of input");
    const char c = s[i];
    if (c == '{') return parseObject(out);
    if (c == '[') return parseArray(out);
    if (c == '"') {
      std::string str;
      if (!parseString(str)) return false;
      out = Json(str);
      return true;
    }
    if (s.compare(i, 4, "true") == 0) {
      i += 4;
      out = Json(true);
      return true;
    }
    if (s.compare(i, 5, "false") == 0) {
      i += 5;
      out = Json(false);
      return true;
    }
    if (s.compare(i, 4, "null") == 0) {
      i += 4;
      out = Json();
      return true;
    }
    return parseNumber(out);
  }

  bool parseNumber(Json& out) {
    const std::size_t start = i;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
    while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' ||
                            s[i] == 'E' || s[i] == '-' || s[i] == '+')) {
      ++i;
    }
    if (i == start) return fail("expected a value");
    const std::string token = s.substr(start, i - start);
    char* end = nullptr;
    const double v = std::strtod(token.c_str(), &end);
    if (end == token.c_str()) return fail("malformed number '" + token + "'");
    out = Json(v);
    return true;
  }

  bool parseString(std::string& out) {
    if (s[i] != '"') return fail("expected a string");
    ++i;
    out.clear();
    while (i < s.size() && s[i] != '"') {
      char c = s[i];
      if (c == '\\') {
        ++i;
        if (i >= s.size()) return fail("unterminated escape");
        switch (s[i]) {
          case 'n': out.push_back('\n'); break;
          case 't': out.push_back('\t'); break;
          case 'r': out.push_back('\r'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case '/': out.push_back('/'); break;
          case 'u': {
            if (i + 4 >= s.size()) return fail("truncated \\u escape");
            const std::string hex = s.substr(i + 1, 4);
            const unsigned long cp = std::strtoul(hex.c_str(), nullptr, 16);
            i += 4;
            // UTF-8 encode the basic multilingual plane; surrogate pairs are
            // outside the scope of the configuration files this reads.
            if (cp < 0x80) {
              out.push_back(static_cast<char>(cp));
            } else if (cp < 0x800) {
              out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
              out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
              out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
              out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
              out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            break;
          }
          default: return fail("unknown escape");
        }
        ++i;
      } else {
        out.push_back(c);
        ++i;
      }
    }
    if (i >= s.size()) return fail("unterminated string");
    ++i;
    return true;
  }

  bool parseArray(Json& out) {
    out = Json::array();
    ++i;  // '['
    skip();
    if (i < s.size() && s[i] == ']') {
      ++i;
      return true;
    }
    while (true) {
      Json v;
      if (!parseValue(v)) return false;
      out.push(v);
      skip();
      if (i < s.size() && s[i] == ',') {
        ++i;
        continue;
      }
      if (i < s.size() && s[i] == ']') {
        ++i;
        return true;
      }
      return fail("expected ',' or ']'");
    }
  }

  bool parseObject(Json& out) {
    out = Json::object();
    ++i;  // '{'
    skip();
    if (i < s.size() && s[i] == '}') {
      ++i;
      return true;
    }
    while (true) {
      skip();
      std::string key;
      if (i >= s.size() || s[i] != '"') return fail("expected a key");
      if (!parseString(key)) return false;
      skip();
      if (i >= s.size() || s[i] != ':') return fail("expected ':'");
      ++i;
      Json v;
      if (!parseValue(v)) return false;
      out[key] = v;
      skip();
      if (i < s.size() && s[i] == ',') {
        ++i;
        continue;
      }
      if (i < s.size() && s[i] == '}') {
        ++i;
        return true;
      }
      return fail("expected ',' or '}'");
    }
  }
};

void escapeInto(const std::string& in, std::string& out) {
  out.push_back('"');
  for (char c : in) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      case '\r': out += "\\r"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
          out += buf;
        } else {
          out.push_back(c);
        }
    }
  }
  out.push_back('"');
}

}  // namespace

std::string jsonNumber(double v) {
  if (!std::isfinite(v)) return "null";
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%.17g", v);
  return std::string(buf);
}

const Json& Json::operator[](const std::string& key) const {
  if (type_ != Type::kObject) return nullJson();
  auto it = object_.find(key);
  return it == object_.end() ? nullJson() : it->second;
}

Json& Json::operator[](const std::string& key) {
  type_ = Type::kObject;
  return object_[key];
}

const Json& Json::at(std::size_t index) const {
  if (type_ != Type::kArray || index >= array_.size()) return nullJson();
  return array_[index];
}

std::size_t Json::size() const {
  if (type_ == Type::kArray) return array_.size();
  if (type_ == Type::kObject) return object_.size();
  return 0;
}

void Json::push(const Json& v) {
  type_ = Type::kArray;
  array_.push_back(v);
}

bool Json::getDouble(const std::string& key, double& out) const {
  const Json& v = (*this)[key];
  if (!v.isNumber()) return false;
  out = v.number_;
  return true;
}

bool Json::getInt(const std::string& key, int& out) const {
  double d = 0.0;
  if (!getDouble(key, d)) return false;
  out = static_cast<int>(d);
  return true;
}

bool Json::getUint64(const std::string& key, unsigned long long& out) const {
  double d = 0.0;
  if (!getDouble(key, d)) return false;
  if (d < 0.0) return false;
  out = static_cast<unsigned long long>(d);
  return true;
}

bool Json::getBool(const std::string& key, bool& out) const {
  const Json& v = (*this)[key];
  if (!v.isBool()) return false;
  out = v.bool_;
  return true;
}

bool Json::getString(const std::string& key, std::string& out) const {
  const Json& v = (*this)[key];
  if (!v.isString()) return false;
  out = v.string_;
  return true;
}

void Json::dumpTo(std::string& out, int indent, int depth) const {
  const std::string pad(static_cast<std::size_t>(indent * depth), ' ');
  const std::string pad_inner(static_cast<std::size_t>(indent * (depth + 1)), ' ');
  const char* nl = indent > 0 ? "\n" : "";
  switch (type_) {
    case Type::kNull: out += "null"; break;
    case Type::kBool: out += bool_ ? "true" : "false"; break;
    case Type::kNumber: out += jsonNumber(number_); break;
    case Type::kString: escapeInto(string_, out); break;
    case Type::kArray: {
      if (array_.empty()) {
        out += "[]";
        break;
      }
      out += "[";
      out += nl;
      for (std::size_t k = 0; k < array_.size(); ++k) {
        out += pad_inner;
        array_[k].dumpTo(out, indent, depth + 1);
        if (k + 1 < array_.size()) out += ",";
        out += nl;
      }
      out += pad;
      out += "]";
      break;
    }
    case Type::kObject: {
      if (object_.empty()) {
        out += "{}";
        break;
      }
      out += "{";
      out += nl;
      std::size_t k = 0;
      for (const auto& kv : object_) {
        out += pad_inner;
        escapeInto(kv.first, out);
        out += indent > 0 ? ": " : ":";
        kv.second.dumpTo(out, indent, depth + 1);
        if (++k < object_.size()) out += ",";
        out += nl;
      }
      out += pad;
      out += "}";
      break;
    }
  }
}

std::string Json::dump(int indent) const {
  std::string out;
  dumpTo(out, indent, 0);
  return out;
}

bool Json::parse(const std::string& text, Json& out, std::string& error) {
  Parser p(text);
  if (!p.parseValue(out)) {
    error = p.error;
    return false;
  }
  p.skip();
  if (p.i != text.size()) {
    error = "trailing content after the top level value";
    return false;
  }
  error.clear();
  return true;
}

bool Json::parseFile(const std::string& path, Json& out, std::string& error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = "cannot open " + path;
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  if (!parse(ss.str(), out, error)) {
    error = path + ": " + error;
    return false;
  }
  return true;
}

bool Json::writeFile(const std::string& path, int indent) const {
  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  f << dump(indent) << "\n";
  return f.good();
}

}  // namespace aerolab
