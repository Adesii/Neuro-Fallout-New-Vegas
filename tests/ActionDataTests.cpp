#include "Actions/ActionData.hpp"
#include "TestFramework.hpp"

using Actions::ActionData;

namespace {

ActionData Parsed(const std::string &source) {
  std::string error;
  auto data = ActionData::Parse(source, error);
  if (!data)
    throw std::runtime_error("parse failed: " + error);
  return std::move(*data);
}

} // namespace

TEST("action data: empty and malformed input is rejected with a message") {
  std::string error;
  CHECK(!ActionData::Parse("", error));
  CHECK(!error.empty());
  error.clear();
  CHECK(!ActionData::Parse("{not json", error));
  CHECK(!error.empty());
}

TEST("action data: objects, size and property lookup") {
  auto data = Parsed(R"({"index":2,"type":"Misc"})");
  CHECK(data.IsObject());
  CHECK_EQ(data.Size(), size_t{2});
  CHECK(data.HasProperty("index"));
  CHECK(data.HasProperty("type"));
  CHECK(!data.HasProperty("ind"));
  CHECK(!data.HasProperty("indexes"));

  CHECK(!Parsed("[1,2]").IsObject());
  CHECK_EQ(Parsed("[1,2]").Size(), size_t{0});
}

TEST("action data: integers must be exact JSON integers that fit") {
  int value = -1;
  CHECK(Parsed(R"({"n":42})").GetInteger("n", value));
  CHECK_EQ(value, 42);
  CHECK(Parsed(R"({"n":-7})").GetInteger("n", value));
  CHECK_EQ(value, -7);

  value = 99;
  CHECK(!Parsed(R"({"n":1.5})").GetInteger("n", value));
  CHECK(!Parsed(R"({"n":"3"})").GetInteger("n", value));
  CHECK(!Parsed(R"({"n":true})").GetInteger("n", value));
  CHECK(!Parsed(R"({"n":99999999999})").GetInteger("n", value));
  CHECK(!Parsed(R"({"m":1})").GetInteger("n", value));
  CHECK(!Parsed("[1]").GetInteger("n", value));
}

TEST("action data: strings are returned verbatim, other types are refused") {
  std::string value;
  CHECK(Parsed(R"({"type":"Weapons"})").GetString("type", value));
  CHECK_EQ(value, std::string("Weapons"));
  CHECK(Parsed(R"({"type":"a\nb"})").GetString("type", value));
  CHECK_EQ(value, std::string("a\nb"));
  CHECK(!Parsed(R"({"type":3})").GetString("type", value));
  CHECK(!Parsed(R"({"other":"x"})").GetString("type", value));
}

TEST("action data: parameterless actions accept missing data or an empty object only") {
  std::string error;
  CHECK(ActionData::ValidateNoParameters("", error));
  CHECK(ActionData::ValidateNoParameters("{}", error));
  CHECK(!ActionData::ValidateNoParameters(R"({"a":1})", error));
  CHECK(!ActionData::ValidateNoParameters("[]", error));
  CHECK(!ActionData::ValidateNoParameters("nonsense", error));
}

TEST("action data: moving transfers ownership") {
  auto first = Parsed(R"({"n":1})");
  ActionData second = std::move(first);
  int value = 0;
  CHECK(second.GetInteger("n", value));
  CHECK(!first.IsObject());
}

TEST("action data: integer edge cases (negative zero, 1.0, 1e2, duplicate keys, huge values)") {
  int value = 5;
  CHECK(Parsed(R"({"n":-0})").GetInteger("n", value));
  CHECK_EQ(value, 0);
  CHECK(!Parsed(R"({"n":1.0})").GetInteger("n", value));
  CHECK(!Parsed(R"({"n":1e2})").GetInteger("n", value));
  CHECK(Parsed(R"({"n":1,"n":2})").GetInteger("n", value));
  CHECK_EQ(value, 1); // the first duplicate wins
  CHECK(!Parsed(R"({"n":2147483648})").GetInteger("n", value));
  CHECK(Parsed(R"({"n":2147483647})").GetInteger("n", value));
  CHECK_EQ(value, 2147483647);
  CHECK(Parsed(R"({"n":-2147483648})").GetInteger("n", value));
}

TEST("action data: the reason an integer failed is distinguishable") {
  using R = ActionData::IntegerResult;
  int value = 0;
  CHECK(Parsed(R"({"n":3})").ReadInteger("n", value) == R::Ok);
  CHECK(Parsed(R"({"m":3})").ReadInteger("n", value) == R::Missing);
  CHECK(Parsed(R"({"n":"3"})").ReadInteger("n", value) == R::NotAnInteger);
  CHECK(Parsed(R"({"n":1.5})").ReadInteger("n", value) == R::NotAnInteger);
  CHECK(Parsed(R"({"n":99999999999})").ReadInteger("n", value) == R::OutOfRange);
}

TEST("action data: a single positive id") {
  int id = 0;
  std::string error;
  CHECK(ActionData::ParsePositiveId(R"({"id":4})", "id", id, error));
  CHECK_EQ(id, 4);
  for (const char *bad : {"", "{}", R"({"id":0})", R"({"id":-3})", R"({"id":"4"})", R"({"id":1.5})", R"({"other":4})",
                          R"({"id":4,"extra":1})", "[4]"}) {
    error.clear();
    CHECK(!ActionData::ParsePositiveId(bad, "id", id, error));
    CHECK(!error.empty());
  }
  CHECK_EQ(id, 4); // a failed parse leaves the output alone
  error.clear();
  CHECK(!ActionData::ParsePositiveId(R"({"id":0})", "id", id, error));
  CHECK(error.find("positive") != std::string::npos);
  error.clear();
  CHECK(!ActionData::ParsePositiveId(R"({"id":99999999999})", "id", id, error));
  CHECK(error.find("too large") != std::string::npos);
}
