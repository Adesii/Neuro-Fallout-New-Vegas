#include "Actions/Json/JsonSchema.hpp"
#include "TestFramework.hpp"

using Actions::Json::JsonSchema;
using Actions::Json::JsonSchemaType;

TEST("schema: default schema is empty and serializes to {}") {
  JsonSchema schema;
  CHECK(schema.IsEmpty());
  CHECK_EQ(schema.Serialize(), std::string("{}"));
  CHECK(schema.GetType() == JsonSchemaType::None);
}

TEST("schema: object with typed, bounded, required properties") {
  auto schema = JsonSchema::Object();
  auto index = JsonSchema::Integer();
  index.Minimum(0).Maximum(9);
  schema.Property("index", std::move(index));
  CHECK(!schema.IsEmpty());
  CHECK_EQ(schema.Serialize(),
           std::string(R"({"type":"object","properties":{"index":{"type":"integer","minimum":0,"maximum":9}},)"
                       R"("required":["index"]})"));
}

TEST("schema: optional properties are not required and properties are sorted by name") {
  auto schema = JsonSchema::Object();
  schema.Property("zeta", JsonSchema::String(), false);
  schema.Property("alpha", JsonSchema::Boolean(), true);
  CHECK_EQ(schema.Serialize(),
           std::string(R"({"type":"object","properties":{"alpha":{"type":"boolean"},"zeta":{"type":"string"}},)"
                       R"("required":["alpha"]})"));
}

TEST("schema: required names are not duplicated and redefining a property replaces it") {
  auto schema = JsonSchema::Object();
  schema.Property("id", JsonSchema::String());
  schema.Property("id", JsonSchema::Integer());
  schema.Required("id");
  CHECK_EQ(schema.Serialize(),
           std::string(R"({"type":"object","properties":{"id":{"type":"integer"}},"required":["id"]})"));
}

TEST("schema: enum values keep their types") {
  auto schema = JsonSchema::String();
  schema.Enum({std::string("All"), std::string("Misc")});
  CHECK_EQ(schema.Serialize(), std::string(R"({"type":"string","enum":["All","Misc"]})"));

  auto mixed = JsonSchema();
  mixed.Enum({nullptr, true, 3, 1.5, std::string("x")});
  CHECK_EQ(mixed.Serialize(), std::string(R"({"enum":[null,true,3,1.5,"x"]})"));
}

TEST("schema: strings are escaped, including control characters") {
  auto schema = JsonSchema::String();
  schema.Pattern("a\"b\\c\n\t\x01");
  CHECK_EQ(schema.Serialize(), std::string(R"({"type":"string","pattern":"a\"b\\c\n\t\u0001"})"));
}

TEST("schema: numeric bounds use a locale independent format") {
  auto schema = JsonSchema::Number();
  schema.Minimum(0.5).ExclusiveMaximum(1000000);
  CHECK_EQ(schema.Serialize(), std::string(R"({"type":"number","minimum":0.5,"exclusiveMaximum":1000000})"));
}

TEST("schema: arrays with items and size limits") {
  auto schema = JsonSchema::Array(JsonSchema::Integer());
  schema.MinItems(1).MaxItems(3).UniqueItems();
  CHECK_EQ(schema.Serialize(),
           std::string(R"({"type":"array","items":{"type":"integer"},"minItems":1,"maxItems":3,"uniqueItems":true})"));
}

TEST("schema: an untyped item schema leaves items out") {
  auto schema = JsonSchema::Array(JsonSchema());
  CHECK_EQ(schema.Serialize(), std::string(R"({"type":"array"})"));
}

TEST("schema: string constraints and const") {
  auto schema = JsonSchema::String();
  schema.MinLength(1).MaxLength(8).Format("date").Const(std::string("fixed"));
  CHECK_EQ(schema.Serialize(),
           std::string(R"({"type":"string","const":"fixed","minLength":1,"maxLength":8,"format":"date"})"));
}
