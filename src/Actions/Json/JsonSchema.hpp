#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace Actions::Json {

enum class JsonSchemaType { None, String, Number, Integer, Boolean, Object, Array, Null };

class JsonSchema {
public:
  using Value = std::variant<std::nullptr_t, bool, int, double, std::string>;

  JsonSchema();

  static JsonSchema Type(JsonSchemaType type);
  static JsonSchema String();
  static JsonSchema Number();
  static JsonSchema Integer();
  static JsonSchema Boolean();
  static JsonSchema Object();
  static JsonSchema Array();
  static JsonSchema Array(JsonSchema items);

  JsonSchema &Property(std::string name, JsonSchema schema, bool required = true);
  JsonSchema &Required(std::string name);
  JsonSchema &Items(JsonSchema schema);

  JsonSchema &Enum(std::vector<Value> values);
  JsonSchema &Const(Value value);
  JsonSchema &Minimum(double value);
  JsonSchema &Maximum(double value);
  JsonSchema &ExclusiveMinimum(double value);
  JsonSchema &ExclusiveMaximum(double value);
  JsonSchema &MinLength(int value);
  JsonSchema &MaxLength(int value);
  JsonSchema &Pattern(std::string value);
  JsonSchema &Format(std::string value);
  JsonSchema &MinItems(int value);
  JsonSchema &MaxItems(int value);
  JsonSchema &UniqueItems(bool value = true);

  JsonSchemaType GetType() const;
  bool IsEmpty() const;
  std::string Serialize() const;

private:
  JsonSchemaType m_type = JsonSchemaType::None;
  std::map<std::string, JsonSchema> m_properties;
  std::shared_ptr<JsonSchema> m_items;
  std::vector<std::string> m_required;
  std::vector<Value> m_enum;
  std::optional<Value> m_const;
  std::optional<double> m_minimum;
  std::optional<double> m_maximum;
  std::optional<double> m_exclusiveMinimum;
  std::optional<double> m_exclusiveMaximum;
  std::optional<int> m_minLength;
  std::optional<int> m_maxLength;
  std::optional<int> m_minItems;
  std::optional<int> m_maxItems;
  std::optional<bool> m_uniqueItems;
  std::optional<std::string> m_pattern;
  std::optional<std::string> m_format;
};

} // namespace Actions::Json
