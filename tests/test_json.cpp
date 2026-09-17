#include "harness.hpp"

#include <cmath>
#include <string>

#include "relay/protocol/json.hpp"

namespace {

using oak::relay::protocol::json::parse;
using oak::relay::protocol::json::Value;

/// Fetches a member, failing the test rather than returning null.
const Value& member(const Value& value, const std::string& key) {
  const Value* found = value.find(key);
  OAK_CHECK(found != nullptr);
  return *found;
}

std::string nestedArrays(int depth) {
  return std::string(static_cast<std::size_t>(depth), '[') +
         std::string(static_cast<std::size_t>(depth), ']');
}

}  // namespace

// ------------------------------------------------------------------- parsing

OAK_TEST(json_parses_flat_object) {
  const auto value = parse(R"({"a":1,"b":"two","c":true,"d":false,"e":null})");
  OAK_CHECK(value.has_value());
  OAK_CHECK(value->isObject());
  OAK_CHECK_EQ(member(*value, "a").asInt(), 1);
  OAK_CHECK_EQ(member(*value, "b").asString(), std::string("two"));
  OAK_CHECK_EQ(member(*value, "c").asBool(), true);
  OAK_CHECK_EQ(member(*value, "d").asBool(), false);
  OAK_CHECK(member(*value, "e").isNull());
}

OAK_TEST(json_parses_arrays_and_nesting) {
  const auto value = parse(R"({"outer":[1,[2,{"deep":"yes"}],3]})");
  OAK_CHECK(value.has_value());

  const auto& outer = member(*value, "outer");
  OAK_CHECK(outer.isArray());
  OAK_CHECK_EQ(outer.asArray().size(), 3u);
  OAK_CHECK_EQ(outer.asArray()[0].asInt(), 1);
  OAK_CHECK_EQ(member(outer.asArray()[1].asArray()[1], "deep").asString(),
               std::string("yes"));
}

OAK_TEST(json_parses_numbers) {
  OAK_CHECK_EQ(parse("1")->asInt(), 1);
  OAK_CHECK_EQ(parse("-2")->asInt(), -2);
  OAK_CHECK_EQ(parse("0")->asInt(), 0);
  OAK_CHECK(std::fabs(parse("-2.5")->asNumber() + 2.5) < 1e-12);
  OAK_CHECK(std::fabs(parse("1e3")->asNumber() - 1000.0) < 1e-9);
  OAK_CHECK(std::fabs(parse("1.5e-2")->asNumber() - 0.015) < 1e-12);
  OAK_CHECK(std::fabs(parse("0.82")->asNumber() - 0.82) < 1e-12);
}

OAK_TEST(json_decodes_string_escapes) {
  OAK_CHECK_EQ(parse(R"("a\nb")")->asString(), std::string("a\nb"));
  OAK_CHECK_EQ(parse(R"("quote\"inside")")->asString(),
               std::string("quote\"inside"));
  OAK_CHECK_EQ(parse(R"("back\\slash")")->asString(),
               std::string("back\\slash"));
  OAK_CHECK_EQ(parse(R"("\u0041")")->asString(), std::string("A"));
  OAK_CHECK_EQ(parse(R"("solidus\/here")")->asString(),
               std::string("solidus/here"));
}

OAK_TEST(json_decodes_surrogate_pairs) {
  // U+1F600, which cannot be expressed in the Basic Multilingual Plane.
  const auto value = parse(R"("\ud83d\ude00")");
  OAK_CHECK(value.has_value());
  OAK_CHECK_EQ(value->asString(), std::string("\xf0\x9f\x98\x80"));
}

OAK_TEST(json_rejects_lone_surrogates) {
  OAK_CHECK(!parse(R"("\ud83d")").has_value());
  OAK_CHECK(!parse(R"("\ude00")").has_value());
  // A high surrogate followed by something that is not a low surrogate.
  OAK_CHECK(!parse(R"("\ud83d\u0041")").has_value());
}

OAK_TEST(json_rejects_malformed_documents) {
  OAK_CHECK(!parse("").has_value());
  OAK_CHECK(!parse("{").has_value());
  OAK_CHECK(!parse("}").has_value());
  OAK_CHECK(!parse(R"({"a":})").has_value());
  OAK_CHECK(!parse(R"({"a" 1})").has_value());
  OAK_CHECK(!parse(R"({"a":1,})").has_value());
  OAK_CHECK(!parse("[1,]").has_value());
  OAK_CHECK(!parse("[1 2]").has_value());
  OAK_CHECK(!parse("tru").has_value());
  OAK_CHECK(!parse("nul").has_value());
  OAK_CHECK(!parse(R"("unterminated)").has_value());
  OAK_CHECK(!parse(R"("\q")").has_value());
  OAK_CHECK(!parse("01").has_value());
  OAK_CHECK(!parse("-").has_value());
  OAK_CHECK(!parse("1.").has_value());
}

OAK_TEST(json_rejects_trailing_content) {
  // A frame payload must be exactly one document; anything after it means the
  // two ends disagree about where the value ends.
  OAK_CHECK(!parse(R"({"a":1}x)").has_value());
  OAK_CHECK(!parse("1 2").has_value());
}

OAK_TEST(json_rejects_raw_control_characters_in_strings) {
  std::string text = "\"a";
  text.push_back('\x01');
  text += "b\"";
  OAK_CHECK(!parse(text).has_value());
}

OAK_TEST(json_enforces_a_depth_limit) {
  OAK_CHECK(parse(nestedArrays(10)).has_value());
  OAK_CHECK(!parse(nestedArrays(200)).has_value());
}

// ------------------------------------------------------------------ structure

OAK_TEST(json_object_equality_ignores_member_order) {
  const auto first = parse(R"({"x":1,"y":2})");
  const auto second = parse(R"({"y":2,"x":1})");
  OAK_CHECK(first.has_value() && second.has_value());
  OAK_CHECK(*first == *second);
}

OAK_TEST(json_duplicate_key_takes_the_last_occurrence) {
  const auto value = parse(R"({"x":1,"x":2})");
  OAK_CHECK(value.has_value());
  OAK_CHECK_EQ(member(*value, "x").asInt(), 2);
}

OAK_TEST(json_missing_member_returns_null_pointer) {
  const auto value = parse(R"({"a":1})");
  OAK_CHECK(value->find("b") == nullptr);
}

OAK_TEST(json_readers_fall_back_on_kind_mismatch) {
  const auto value = parse(R"({"n":1,"s":"text"})");
  // Asking a number for its string form must not throw or convert.
  OAK_CHECK_EQ(member(*value, "n").asString("fallback"), std::string("fallback"));
  OAK_CHECK_EQ(member(*value, "s").asInt(-1), -1);
}

OAK_TEST(json_find_on_non_object_returns_null) {
  const auto value = parse("[1,2,3]");
  OAK_CHECK(value->find("a") == nullptr);
}

// ----------------------------------------------------------------- dumping

OAK_TEST(json_dump_round_trips) {
  const auto original = parse(R"({"a":[1,2,3],"b":"x\ny","c":true,"d":null,"e":0.5})");
  OAK_CHECK(original.has_value());

  const auto reparsed = parse(original->dump());
  OAK_CHECK(reparsed.has_value());
  OAK_CHECK(*original == *reparsed);
}

OAK_TEST(json_dump_escapes_what_it_must) {
  Value value = Value::string("quote\" back\\ newline\n tab\t");
  const std::string text = value.dump();
  OAK_CHECK_EQ(text, std::string("\"quote\\\" back\\\\ newline\\n tab\\t\""));

  const auto reparsed = parse(text);
  OAK_CHECK(reparsed.has_value());
  OAK_CHECK(*reparsed == value);
}

OAK_TEST(json_dump_writes_integers_without_a_decimal_point) {
  OAK_CHECK_EQ(Value::integer(5).dump(), std::string("5"));
  OAK_CHECK_EQ(Value::integer(-7).dump(), std::string("-7"));
  OAK_CHECK_EQ(Value::integer(0).dump(), std::string("0"));
}

OAK_TEST(json_dump_writes_null_for_non_finite_numbers) {
  // Valid JSON has no way to spell infinity, and emitting "inf" would produce
  // a document the peer cannot parse.
  OAK_CHECK_EQ(Value::number(std::nan("")).dump(), std::string("null"));
  OAK_CHECK_EQ(Value::number(HUGE_VAL).dump(), std::string("null"));
}

OAK_TEST(json_dump_preserves_utf8) {
  const std::string text = "\xe6\xb7\xb1\xe5\xa4\x9c";  // three-byte sequence
  const Value value = Value::string(text);
  OAK_CHECK_EQ(value.dump(), "\"" + text + "\"");
}

OAK_TEST(json_set_replaces_an_existing_key) {
  Value value = Value::object();
  value.set("a", Value::integer(1));
  value.set("b", Value::integer(2));
  value.set("a", Value::integer(3));
  OAK_CHECK_EQ(value.members().size(), 2u);
  OAK_CHECK_EQ(member(value, "a").asInt(), 3);
}
