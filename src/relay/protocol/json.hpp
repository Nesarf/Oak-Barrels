// A minimal JSON value type, sized for this protocol and nothing else.
//
// Why hand-rolled instead of a dependency: payloads here are small, flat, and
// entirely under this protocol's control, and a relay that links somebody
// else's library is a relay that has to explain that library at audit time.
//
// Deliberate scope limits, all of which are safe because the protocol only
// ever emits and accepts what is listed:
//   - Numbers are doubles. Integers in this protocol stay far below 2^53.
//   - Object members keep insertion order and are looked up linearly.
//     Payloads have a handful of fields; a tree is not worth it.
//   - No duplicate-key merging: the last occurrence wins.
//
// Not modelled: nothing. The grammar accepted here is complete JSON
// (RFC 8259) apart from the number-precision note above.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oak::relay::protocol::json {

/// A JSON value. Default-constructed is null.
///
/// Note the storage: `array_` and `object_` are containers of an incomplete
/// type (`Value` itself). `std::vector` explicitly permits that; `std::map`
/// does not, which is why members are a vector of pairs rather than a map.
class Value {
 public:
  enum class Kind { Null, Bool, Number, String, Array, Object };

  Value() = default;

  static Value null() { return Value(); }
  static Value boolean(bool v);
  static Value number(double v);
  static Value integer(std::int64_t v);
  static Value string(std::string v);
  static Value array(std::vector<Value> items = {});
  static Value object();

  Kind kind() const { return kind_; }

  bool isNull() const { return kind_ == Kind::Null; }
  bool isBool() const { return kind_ == Kind::Bool; }
  bool isNumber() const { return kind_ == Kind::Number; }
  bool isString() const { return kind_ == Kind::String; }
  bool isArray() const { return kind_ == Kind::Array; }
  bool isObject() const { return kind_ == Kind::Object; }

  // Readers. Each takes a fallback and returns it when the kind does not
  // match, so callers never have to check first. A malformed payload from a
  // peer degrades to a default rather than throwing across the wire.
  bool asBool(bool fallback = false) const;
  double asNumber(double fallback = 0.0) const;
  std::int64_t asInt(std::int64_t fallback = 0) const;
  std::string asString(std::string fallback = {}) const;
  const std::vector<Value>& asArray() const;

  // Object access. `find` returns nullptr when absent or when this is not an
  // object, so `find("x") ? ... : ...` is the intended idiom.
  const Value* find(const std::string& key) const;
  void set(std::string key, Value value);
  const std::vector<std::pair<std::string, Value>>& members() const { return object_; }

  /// Serialises to compact JSON text.
  std::string dump() const;

  bool operator==(const Value& other) const;
  bool operator!=(const Value& other) const { return !(*this == other); }

 private:
  Kind kind_ = Kind::Null;
  bool bool_ = false;
  double number_ = 0.0;
  std::string string_;
  std::vector<Value> array_;
  std::vector<std::pair<std::string, Value>> object_;
};

/// Parses one complete JSON document.
///
/// Returns nullopt on any syntax error, trailing content, or nesting deeper
/// than the internal limit. Never throws.
std::optional<Value> parse(std::string_view text);

/// Depth limit applied by `parse`. Exposed so tests can assert the boundary.
inline constexpr int kMaxParseDepth = 64;

}  // namespace oak::relay::protocol::json
