#include "relay/protocol/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace oak::relay::protocol::json {
namespace {

void appendUtf8(std::string& out, std::uint32_t codePoint) {
  if (codePoint <= 0x7f) {
    out.push_back(static_cast<char>(codePoint));
  } else if (codePoint <= 0x7ff) {
    out.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
  } else if (codePoint <= 0xffff) {
    out.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
  } else {
    out.push_back(static_cast<char>(0xf0 | (codePoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
  }
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool isJsonWhitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/// Recursive-descent parser over a complete document.
///
/// Every failure path returns nullopt; nothing throws and no position is
/// reported, because the only useful thing a peer can do with a malformed
/// frame is refuse it.
class Parser {
 public:
  explicit Parser(std::string_view text) : text_(text) {}

  std::optional<Value> parseDocument() {
    skipWhitespace();
    auto value = parseValue(0);
    if (!value.has_value()) return std::nullopt;
    skipWhitespace();
    if (pos_ != text_.size()) return std::nullopt;  // trailing content
    return value;
  }

 private:
  void skipWhitespace() {
    while (pos_ < text_.size() && isJsonWhitespace(text_[pos_])) ++pos_;
  }

  std::optional<Value> parseValue(int depth) {
    if (depth > kMaxParseDepth) return std::nullopt;
    if (pos_ >= text_.size()) return std::nullopt;

    switch (text_[pos_]) {
      case '{':
        return parseObject(depth);
      case '[':
        return parseArray(depth);
      case '"': {
        auto text = parseString();
        if (!text.has_value()) return std::nullopt;
        return Value::string(std::move(*text));
      }
      case 't':
        if (consumeLiteral("true")) return Value::boolean(true);
        return std::nullopt;
      case 'f':
        if (consumeLiteral("false")) return Value::boolean(false);
        return std::nullopt;
      case 'n':
        if (consumeLiteral("null")) return Value::null();
        return std::nullopt;
      default:
        return parseNumber();
    }
  }

  bool consumeLiteral(std::string_view literal) {
    if (text_.size() - pos_ < literal.size()) return false;
    if (text_.compare(pos_, literal.size(), literal) != 0) return false;
    pos_ += literal.size();
    return true;
  }

  std::optional<Value> parseObject(int depth) {
    ++pos_;  // '{'
    Value object = Value::object();
    skipWhitespace();
    if (pos_ < text_.size() && text_[pos_] == '}') {
      ++pos_;
      return object;
    }

    while (true) {
      skipWhitespace();
      if (pos_ >= text_.size() || text_[pos_] != '"') return std::nullopt;

      auto key = parseString();
      if (!key.has_value()) return std::nullopt;

      skipWhitespace();
      if (pos_ >= text_.size() || text_[pos_] != ':') return std::nullopt;
      ++pos_;

      skipWhitespace();
      auto value = parseValue(depth + 1);
      if (!value.has_value()) return std::nullopt;

      object.set(std::move(*key), std::move(*value));

      skipWhitespace();
      if (pos_ >= text_.size()) return std::nullopt;
      if (text_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (text_[pos_] == '}') {
        ++pos_;
        return object;
      }
      return std::nullopt;
    }
  }

  std::optional<Value> parseArray(int depth) {
    ++pos_;  // '['
    std::vector<Value> items;
    skipWhitespace();
    if (pos_ < text_.size() && text_[pos_] == ']') {
      ++pos_;
      return Value::array(std::move(items));
    }

    while (true) {
      skipWhitespace();
      auto value = parseValue(depth + 1);
      if (!value.has_value()) return std::nullopt;
      items.push_back(std::move(*value));

      skipWhitespace();
      if (pos_ >= text_.size()) return std::nullopt;
      if (text_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (text_[pos_] == ']') {
        ++pos_;
        return Value::array(std::move(items));
      }
      return std::nullopt;
    }
  }

  std::optional<std::uint32_t> parseHex4() {
    if (text_.size() - pos_ < 4) return std::nullopt;
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = text_[pos_++];
      value <<= 4;
      if (c >= '0' && c <= '9') {
        value |= static_cast<std::uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        value |= static_cast<std::uint32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        value |= static_cast<std::uint32_t>(c - 'A' + 10);
      } else {
        return std::nullopt;
      }
    }
    return value;
  }

  std::optional<std::string> parseString() {
    ++pos_;  // opening quote
    std::string out;

    while (true) {
      if (pos_ >= text_.size()) return std::nullopt;
      const unsigned char c = static_cast<unsigned char>(text_[pos_]);

      if (c == '"') {
        ++pos_;
        return out;
      }

      if (c == '\\') {
        ++pos_;
        if (pos_ >= text_.size()) return std::nullopt;
        const char escape = text_[pos_++];
        switch (escape) {
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case '/': out.push_back('/'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case 'n': out.push_back('\n'); break;
          case 'r': out.push_back('\r'); break;
          case 't': out.push_back('\t'); break;
          case 'u': {
            auto first = parseHex4();
            if (!first.has_value()) return std::nullopt;
            std::uint32_t codePoint = *first;

            if (codePoint >= 0xd800 && codePoint <= 0xdbff) {
              // High surrogate: a following low surrogate is mandatory.
              if (text_.size() - pos_ < 2 || text_[pos_] != '\\' || text_[pos_ + 1] != 'u') {
                return std::nullopt;
              }
              pos_ += 2;
              auto second = parseHex4();
              if (!second.has_value()) return std::nullopt;
              if (*second < 0xdc00 || *second > 0xdfff) return std::nullopt;
              codePoint = 0x10000u + ((codePoint - 0xd800u) << 10) + (*second - 0xdc00u);
            } else if (codePoint >= 0xdc00 && codePoint <= 0xdfff) {
              return std::nullopt;  // lone low surrogate
            }

            appendUtf8(out, codePoint);
            break;
          }
          default:
            return std::nullopt;
        }
        continue;
      }

      // Unescaped control characters are not legal inside a JSON string.
      if (c < 0x20) return std::nullopt;

      // Any other byte is copied through: the payload is UTF-8 by contract
      // and this parser does not re-validate the encoding.
      out.push_back(static_cast<char>(c));
      ++pos_;
    }
  }

  std::optional<Value> parseNumber() {
    const std::size_t start = pos_;

    if (pos_ < text_.size() && text_[pos_] == '-') ++pos_;

    if (pos_ >= text_.size()) return std::nullopt;
    if (text_[pos_] == '0') {
      ++pos_;  // a leading zero may not be followed by more digits
    } else if (isDigit(text_[pos_])) {
      while (pos_ < text_.size() && isDigit(text_[pos_])) ++pos_;
    } else {
      return std::nullopt;
    }

    if (pos_ < text_.size() && text_[pos_] == '.') {
      ++pos_;
      if (pos_ >= text_.size() || !isDigit(text_[pos_])) return std::nullopt;
      while (pos_ < text_.size() && isDigit(text_[pos_])) ++pos_;
    }

    if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
      if (pos_ >= text_.size() || !isDigit(text_[pos_])) return std::nullopt;
      while (pos_ < text_.size() && isDigit(text_[pos_])) ++pos_;
    }

    const std::string token(text_.substr(start, pos_ - start));
    char* end = nullptr;
    const double parsed = std::strtod(token.c_str(), &end);
    if (end == nullptr || *end != '\0') return std::nullopt;
    return Value::number(parsed);
  }

  std::string_view text_;
  std::size_t pos_ = 0;
};

void writeEscapedString(const std::string& value, std::string& out) {
  out.push_back('"');
  for (const char raw : value) {
    const unsigned char c = static_cast<unsigned char>(raw);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
          out += buffer;
        } else {
          out.push_back(raw);  // UTF-8 passes through unescaped
        }
        break;
    }
  }
  out.push_back('"');
}

void writeNumber(double value, std::string& out) {
  // The protocol never produces these; emitting null keeps the output valid
  // JSON rather than printing "nan", which no parser would accept.
  if (!std::isfinite(value)) {
    out += "null";
    return;
  }

  if (value == std::floor(value) && std::fabs(value) < 1e15) {
    out += std::to_string(static_cast<std::int64_t>(value));
    return;
  }

  char buffer[40];
  std::snprintf(buffer, sizeof(buffer), "%.17g", value);
  out += buffer;
}

void writeValue(const Value& value, std::string& out) {
  switch (value.kind()) {
    case Value::Kind::Null:
      out += "null";
      return;
    case Value::Kind::Bool:
      out += value.asBool() ? "true" : "false";
      return;
    case Value::Kind::Number:
      writeNumber(value.asNumber(), out);
      return;
    case Value::Kind::String:
      writeEscapedString(value.asString(), out);
      return;
    case Value::Kind::Array: {
      out.push_back('[');
      bool first = true;
      for (const Value& item : value.asArray()) {
        if (!first) out.push_back(',');
        first = false;
        writeValue(item, out);
      }
      out.push_back(']');
      return;
    }
    case Value::Kind::Object: {
      out.push_back('{');
      bool first = true;
      for (const auto& member : value.members()) {
        if (!first) out.push_back(',');
        first = false;
        writeEscapedString(member.first, out);
        out.push_back(':');
        writeValue(member.second, out);
      }
      out.push_back('}');
      return;
    }
  }
}

}  // namespace

Value Value::boolean(bool v) {
  Value value;
  value.kind_ = Kind::Bool;
  value.bool_ = v;
  return value;
}

Value Value::number(double v) {
  Value value;
  value.kind_ = Kind::Number;
  value.number_ = v;
  return value;
}

Value Value::integer(std::int64_t v) {
  return Value::number(static_cast<double>(v));
}

Value Value::string(std::string v) {
  Value value;
  value.kind_ = Kind::String;
  value.string_ = std::move(v);
  return value;
}

Value Value::array(std::vector<Value> items) {
  Value value;
  value.kind_ = Kind::Array;
  value.array_ = std::move(items);
  return value;
}

Value Value::object() {
  Value value;
  value.kind_ = Kind::Object;
  return value;
}

bool Value::asBool(bool fallback) const {
  return kind_ == Kind::Bool ? bool_ : fallback;
}

double Value::asNumber(double fallback) const {
  return kind_ == Kind::Number ? number_ : fallback;
}

std::int64_t Value::asInt(std::int64_t fallback) const {
  if (kind_ != Kind::Number) return fallback;
  if (!std::isfinite(number_)) return fallback;
  return static_cast<std::int64_t>(number_);
}

std::string Value::asString(std::string fallback) const {
  return kind_ == Kind::String ? string_ : std::move(fallback);
}

const std::vector<Value>& Value::asArray() const { return array_; }

const Value* Value::find(const std::string& key) const {
  if (kind_ != Kind::Object) return nullptr;
  for (const auto& member : object_) {
    if (member.first == key) return &member.second;
  }
  return nullptr;
}

void Value::set(std::string key, Value value) {
  if (kind_ != Kind::Object) return;
  for (auto& member : object_) {
    if (member.first == key) {
      member.second = std::move(value);  // later occurrence wins
      return;
    }
  }
  object_.emplace_back(std::move(key), std::move(value));
}

std::string Value::dump() const {
  std::string out;
  writeValue(*this, out);
  return out;
}

bool Value::operator==(const Value& other) const {
  if (kind_ != other.kind_) return false;
  switch (kind_) {
    case Kind::Null:
      return true;
    case Kind::Bool:
      return bool_ == other.bool_;
    case Kind::Number:
      return number_ == other.number_;
    case Kind::String:
      return string_ == other.string_;
    case Kind::Array:
      return array_ == other.array_;
    case Kind::Object: {
      // Order-insensitive: two objects with the same members are equal even
      // when the members were inserted in a different order.
      if (object_.size() != other.object_.size()) return false;
      for (const auto& member : object_) {
        const Value* mine = other.find(member.first);
        if (mine == nullptr || !(*mine == member.second)) return false;
      }
      return true;
    }
  }
  return false;
}

std::optional<Value> parse(std::string_view text) {
  Parser parser(text);
  return parser.parseDocument();
}

}  // namespace oak::relay::protocol::json
