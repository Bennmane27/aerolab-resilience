#include "aerolab/io/yaml.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

namespace aerolab {
namespace {

struct Line {
  int indent{0};
  std::string text;
  int source_line{0};
  // The body before sequence-item expansion. Block scalars are reassembled from
  // this, so that a folded description is not reinterpreted as structure.
  std::string raw;
};

std::string trimRight(const std::string& s) {
  std::size_t end = s.size();
  while (end > 0 && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r')) --end;
  return s.substr(0, end);
}

std::string trim(const std::string& s) {
  std::size_t begin = 0;
  while (begin < s.size() && (s[begin] == ' ' || s[begin] == '\t')) ++begin;
  return trimRight(s.substr(begin));
}

// Strips a trailing comment, respecting quotes and flow brackets.
std::string stripComment(const std::string& s) {
  bool in_single = false;
  bool in_double = false;
  for (std::size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    if (c == '\'' && !in_double)
      in_single = !in_single;
    else if (c == '"' && !in_single)
      in_double = !in_double;
    else if (c == '#' && !in_single && !in_double) {
      if (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t') return s.substr(0, i);
    }
  }
  return s;
}

class YamlParser {
 public:
  explicit YamlParser(const std::string& text) { tokenize(text); }

  bool run(Json& out) {
    if (!error_.empty()) return false;
    if (lines_.empty()) {
      out = Json::object();
      return true;
    }
    std::size_t i = 0;
    out = parseNode(i, lines_[0].indent);
    if (!error_.empty()) return false;
    if (i != lines_.size()) {
      fail(lines_[i].source_line, "unexpected indentation");
      return false;
    }
    return true;
  }

  const std::string& error() const { return error_; }

 private:
  void fail(int line, const std::string& message) {
    if (error_.empty()) {
      std::ostringstream ss;
      ss << "line " << line << ": " << message;
      error_ = ss.str();
    }
  }

  void tokenize(const std::string& text) {
    std::istringstream in(text);
    std::string raw;
    int number = 0;
    while (std::getline(in, raw)) {
      ++number;
      std::string s = trimRight(stripComment(raw));
      if (trim(s).empty()) continue;
      if (trim(s) == "---") continue;
      int indent = 0;
      while (indent < static_cast<int>(s.size()) && s[static_cast<std::size_t>(indent)] == ' ') {
        ++indent;
      }
      if (s.find('\t') != std::string::npos && s.find_first_not_of(" \t") != std::string::npos &&
          s.find('\t') < s.find_first_not_of(" \t")) {
        fail(number, "tabs are not valid YAML indentation");
        return;
      }
      std::string body = s.substr(static_cast<std::size_t>(indent));
      for (char c : std::string("&*!")) {
        if (!body.empty() && body[0] == c) {
          fail(number, std::string("unsupported YAML construct '") + c + "'");
          return;
        }
      }
      // Expand "- content" into a bare dash plus an indented child so the
      // parser only ever sees one sequence-item shape.
      if (body == "-" || (body.size() > 1 && body[0] == '-' && body[1] == ' ')) {
        lines_.push_back({indent, "-", number, body});
        const std::string rest = trim(body.substr(1));
        if (!rest.empty()) lines_.push_back({indent + 2, rest, number, rest});
      } else {
        lines_.push_back({indent, body, number, body});
      }
    }
  }

  Json parseNode(std::size_t& i, int indent) {
    if (i >= lines_.size()) return Json();
    if (lines_[i].text == "-") return parseSequence(i, indent);
    // A sequence item can be a flow collection in its entirety:
    //   scenarios:
    //     - {path: scenarios/SCN-001.yaml, seeds: 1000}
    // After dash expansion that arrives here as a single line starting with a
    // brace, which is a value rather than a mapping of its own.
    const char first = lines_[i].text.empty() ? 0 : lines_[i].text[0];
    if (first == '{' || first == '[') {
      const Json value = parseScalarOrFlow(lines_[i].text, lines_[i].source_line);
      ++i;
      return value;
    }
    return parseMapping(i, indent);
  }

  Json parseSequence(std::size_t& i, int indent) {
    Json arr = Json::array();
    while (i < lines_.size() && lines_[i].indent == indent && lines_[i].text == "-") {
      ++i;
      if (i < lines_.size() && lines_[i].indent > indent) {
        arr.push(parseNode(i, lines_[i].indent));
      } else {
        arr.push(Json());
      }
      if (!error_.empty()) return arr;
    }
    return arr;
  }

  Json parseMapping(std::size_t& i, int indent) {
    Json obj = Json::object();
    while (i < lines_.size() && lines_[i].indent == indent && lines_[i].text != "-") {
      const Line& line = lines_[i];
      const std::size_t colon = findKeyColon(line.text);
      if (colon == std::string::npos) {
        fail(line.source_line, "expected 'key: value'");
        return obj;
      }
      const std::string key = unquote(trim(line.text.substr(0, colon)));
      const std::string rest = trim(line.text.substr(colon + 1));
      ++i;
      if (rest.empty()) {
        if (i < lines_.size() && lines_[i].indent > indent) {
          obj[key] = parseNode(i, lines_[i].indent);
        } else {
          obj[key] = Json();
        }
      } else if (isBlockScalarIndicator(rest)) {
        obj[key] = Json(readBlockScalar(i, indent, rest[0] == '>'));
      } else {
        obj[key] = parseScalarOrFlow(rest, line.source_line);
      }
      if (!error_.empty()) return obj;
    }
    return obj;
  }

  // Block scalar headers: ">", ">-", ">+", "|", "|-", "|+". Folded ('>') joins
  // the continuation lines with spaces; literal ('|') keeps the newlines.
  // Supported because the acceptance blocks of the scenarios carry several
  // sentences of reasoning each, and squeezing those onto one line would make
  // the files unreadable - which defeats their purpose as the auditable record
  // of what each scenario is asserting.
  static bool isBlockScalarIndicator(const std::string& rest) {
    if (rest.empty()) return false;
    if (rest[0] != '>' && rest[0] != '|') return false;
    if (rest.size() == 1) return true;
    return rest.size() == 2 && (rest[1] == '-' || rest[1] == '+');
  }

  std::string readBlockScalar(std::size_t& i, int parent_indent, bool folded) {
    std::string out;
    bool first = true;
    while (i < lines_.size() && lines_[i].indent > parent_indent) {
      const std::string& piece = lines_[i].raw;
      if (!first) out += folded ? " " : "\n";
      out += piece;
      first = false;
      ++i;
    }
    return out;
  }

  static std::size_t findKeyColon(const std::string& s) {
    bool in_single = false;
    bool in_double = false;
    int depth = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
      const char c = s[i];
      if (c == '\'' && !in_double)
        in_single = !in_single;
      else if (c == '"' && !in_single)
        in_double = !in_double;
      else if (!in_single && !in_double) {
        if (c == '[' || c == '{')
          ++depth;
        else if (c == ']' || c == '}')
          --depth;
        else if (c == ':' && depth == 0) {
          if (i + 1 == s.size() || s[i + 1] == ' ') return i;
        }
      }
    }
    return std::string::npos;
  }

  static std::string unquote(const std::string& s) {
    if (s.size() >= 2 &&
        ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) {
      return s.substr(1, s.size() - 2);
    }
    return s;
  }

  Json parseScalarOrFlow(const std::string& s, int line) {
    if (!s.empty() && (s.front() == '[' || s.front() == '{')) return parseFlow(s, line);
    // An anchor, alias or tag in value position is rejected rather than taken
    // literally. Silently turning "&anchor value" into the string
    // "&anchor value" would be exactly the misinterpretation this parser
    // promises never to make.
    if (!s.empty() && (s.front() == '&' || s.front() == '*' || s.front() == '!')) {
      fail(line, std::string("unsupported YAML construct '") + s.front() + "' in a value");
      return Json();
    }
    return parseScalar(s);
  }

  Json parseFlow(const std::string& s, int line) {
    const char open = s.front();
    const char close = open == '[' ? ']' : '}';
    if (s.back() != close) {
      fail(line, "unterminated flow collection");
      return Json();
    }
    const std::string inner = trim(s.substr(1, s.size() - 2));
    Json result = open == '[' ? Json::array() : Json::object();
    if (inner.empty()) return result;
    for (const std::string& part : splitTopLevel(inner, ',')) {
      const std::string item = trim(part);
      if (item.empty()) continue;
      if (open == '[') {
        result.push(parseScalarOrFlow(item, line));
      } else {
        const std::size_t colon = findKeyColon(item);
        if (colon == std::string::npos) {
          fail(line, "expected 'key: value' inside a flow mapping");
          return result;
        }
        result[unquote(trim(item.substr(0, colon)))] =
            parseScalarOrFlow(trim(item.substr(colon + 1)), line);
      }
    }
    return result;
  }

  static std::vector<std::string> splitTopLevel(const std::string& s, char separator) {
    std::vector<std::string> parts;
    int depth = 0;
    bool in_single = false;
    bool in_double = false;
    std::string current;
    for (char c : s) {
      if (c == '\'' && !in_double)
        in_single = !in_single;
      else if (c == '"' && !in_single)
        in_double = !in_double;
      if (!in_single && !in_double) {
        if (c == '[' || c == '{')
          ++depth;
        else if (c == ']' || c == '}')
          --depth;
        else if (c == separator && depth == 0) {
          parts.push_back(current);
          current.clear();
          continue;
        }
      }
      current.push_back(c);
    }
    parts.push_back(current);
    return parts;
  }

  static Json parseScalar(const std::string& raw) {
    if (raw.size() >= 2 && ((raw.front() == '"' && raw.back() == '"') ||
                            (raw.front() == '\'' && raw.back() == '\''))) {
      return Json(raw.substr(1, raw.size() - 2));
    }
    if (raw == "true" || raw == "True" || raw == "yes") return Json(true);
    if (raw == "false" || raw == "False" || raw == "no") return Json(false);
    if (raw == "null" || raw == "~" || raw.empty()) return Json();
    char* end = nullptr;
    const double v = std::strtod(raw.c_str(), &end);
    if (end != nullptr && *end == '\0' && end != raw.c_str()) return Json(v);
    return Json(raw);
  }

  std::vector<Line> lines_;
  std::string error_;
};

}  // namespace

bool parseYaml(const std::string& text, Json& out, std::string& error) {
  YamlParser p(text);
  if (!p.run(out)) {
    error = p.error().empty() ? "malformed YAML" : p.error();
    return false;
  }
  error.clear();
  return true;
}

bool parseYamlFile(const std::string& path, Json& out, std::string& error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = "cannot open " + path;
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  if (!parseYaml(ss.str(), out, error)) {
    error = path + ": " + error;
    return false;
  }
  return true;
}

bool loadStructuredFile(const std::string& path, Json& out, std::string& error) {
  const std::size_t dot = path.find_last_of('.');
  const std::string ext = dot == std::string::npos ? std::string() : path.substr(dot);
  if (ext == ".json") return Json::parseFile(path, out, error);
  return parseYamlFile(path, out, error);
}

}  // namespace aerolab
