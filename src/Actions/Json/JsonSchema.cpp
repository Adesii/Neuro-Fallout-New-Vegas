#include "JsonSchema.hpp"
#include <algorithm>
#include <iomanip>
#include <locale>
#include <sstream>
#include <type_traits>
#include <utility>

namespace Actions::Json {
namespace {

const char *TypeName(JsonSchemaType type) {
  switch (type) {
  case JsonSchemaType::String:
    return "string";
  case JsonSchemaType::Number:
    return "number";
  case JsonSchemaType::Integer:
    return "integer";
  case JsonSchemaType::Boolean:
    return "boolean";
  case JsonSchemaType::Object:
    return "object";
  case JsonSchemaType::Array:
    return "array";
  case JsonSchemaType::Null:
    return "null";
  default:
    return nullptr;
  }
}

void AppendString(std::string &output, const std::string &value) {
  static constexpr char hex[] = "0123456789abcdef";
  output.push_back('"');
  for (unsigned char character : value) {
    switch (character) {
    case '"':
      output += "\\\"";
      break;
    case '\\':
      output += "\\\\";
      break;
    case '\b':
      output += "\\b";
      break;
    case '\f':
      output += "\\f";
      break;
    case '\n':
      output += "\\n";
      break;
    case '\r':
      output += "\\r";
      break;
    case '\t':
      output += "\\t";
      break;
    default:
      if (character < 0x20) {
        output += "\\u00";
        output.push_back(hex[character >> 4]);
        output.push_back(hex[character & 0x0F]);
      } else {
        output.push_back(static_cast<char>(character));
      }
      break;
    }
  }
  output.push_back('"');
}

void AppendNumber(std::string &output, double value) {
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(15) << value;
  output += stream.str();
}

void AppendValue(std::string &output, const JsonSchema::Value &value) {
  std::visit(
      [&](const auto &entry) {
        using T = std::decay_t<decltype(entry)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>)
          output += "null";
        else if constexpr (std::is_same_v<T, bool>)
          output += entry ? "true" : "false";
        else if constexpr (std::is_same_v<T, int>)
          output += std::to_string(entry);
        else if constexpr (std::is_same_v<T, double>)
          AppendNumber(output, entry);
        else
          AppendString(output, entry);
      },
      value);
}

class ObjectWriter {
public:
  explicit ObjectWriter(std::string &output) : m_output(output) { m_output.push_back('{'); }
  ~ObjectWriter() { Finish(); }

  void Finish() {
    if (!m_finished) {
      m_output.push_back('}');
      m_finished = true;
    }
  }

  template <class WriteValue> void Field(const char *name, WriteValue writeValue) {
    if (!m_first)
      m_output.push_back(',');
    m_first = false;
    AppendString(m_output, name);
    m_output.push_back(':');
    writeValue();
  }

private:
  std::string &m_output;
  bool m_first = true;
  bool m_finished = false;
};

} // namespace

JsonSchema::JsonSchema() = default;

JsonSchema JsonSchema::Type(JsonSchemaType type) {
  JsonSchema schema;
  schema.m_type = type;
  return schema;
}

JsonSchema JsonSchema::String() { return Type(JsonSchemaType::String); }
JsonSchema JsonSchema::Number() { return Type(JsonSchemaType::Number); }
JsonSchema JsonSchema::Integer() { return Type(JsonSchemaType::Integer); }
JsonSchema JsonSchema::Boolean() { return Type(JsonSchemaType::Boolean); }
JsonSchema JsonSchema::Object() { return Type(JsonSchemaType::Object); }

JsonSchema JsonSchema::Array() { return Type(JsonSchemaType::Array); }

JsonSchema JsonSchema::Array(JsonSchema items) {
  auto schema = Type(JsonSchemaType::Array);
  if (items.m_type != JsonSchemaType::None)
    schema.Items(std::move(items));
  return schema;
}

JsonSchema &JsonSchema::Property(std::string name, JsonSchema schema, bool required) {
  auto [iter, inserted] = m_properties.insert_or_assign(std::move(name), std::move(schema));
  if (required)
    Required(iter->first);
  return *this;
}

JsonSchema &JsonSchema::Required(std::string name) {
  if (std::find(m_required.begin(), m_required.end(), name) == m_required.end())
    m_required.push_back(std::move(name));
  return *this;
}

JsonSchema &JsonSchema::Items(JsonSchema schema) {
  m_items = std::make_shared<JsonSchema>(std::move(schema));
  return *this;
}

JsonSchema &JsonSchema::Enum(std::vector<Value> values) {
  m_enum = std::move(values);
  return *this;
}

JsonSchema &JsonSchema::Const(Value value) {
  m_const = std::move(value);
  return *this;
}

JsonSchema &JsonSchema::Minimum(double value) {
  m_minimum = value;
  return *this;
}

JsonSchema &JsonSchema::Maximum(double value) {
  m_maximum = value;
  return *this;
}

JsonSchema &JsonSchema::ExclusiveMinimum(double value) {
  m_exclusiveMinimum = value;
  return *this;
}

JsonSchema &JsonSchema::ExclusiveMaximum(double value) {
  m_exclusiveMaximum = value;
  return *this;
}

JsonSchema &JsonSchema::MinLength(int value) {
  m_minLength = value;
  return *this;
}

JsonSchema &JsonSchema::MaxLength(int value) {
  m_maxLength = value;
  return *this;
}

JsonSchema &JsonSchema::Pattern(std::string value) {
  m_pattern = std::move(value);
  return *this;
}

JsonSchema &JsonSchema::Format(std::string value) {
  m_format = std::move(value);
  return *this;
}

JsonSchema &JsonSchema::MinItems(int value) {
  m_minItems = value;
  return *this;
}

JsonSchema &JsonSchema::MaxItems(int value) {
  m_maxItems = value;
  return *this;
}

JsonSchema &JsonSchema::UniqueItems(bool value) {
  m_uniqueItems = value;
  return *this;
}

JsonSchemaType JsonSchema::GetType() const { return m_type; }

bool JsonSchema::IsEmpty() const {
  return m_type == JsonSchemaType::None && m_properties.empty() && !m_items && m_required.empty() && m_enum.empty() &&
         !m_const && !m_minimum && !m_maximum && !m_exclusiveMinimum && !m_exclusiveMaximum && !m_minLength &&
         !m_maxLength && !m_minItems && !m_maxItems && !m_uniqueItems && !m_pattern && !m_format;
}

std::string JsonSchema::Serialize() const {
  std::string output;
  ObjectWriter object(output);

  if (const char *type = TypeName(m_type))
    object.Field("type", [&]() { AppendString(output, type); });
  if (!m_properties.empty()) {
    object.Field("properties", [&]() {
      ObjectWriter properties(output);
      for (const auto &[name, schema] : m_properties)
        properties.Field(name.c_str(), [&]() { output += schema.Serialize(); });
    });
  }
  if (m_items)
    object.Field("items", [&]() { output += m_items->Serialize(); });
  if (!m_enum.empty()) {
    object.Field("enum", [&]() {
      output.push_back('[');
      for (size_t index = 0; index < m_enum.size(); ++index) {
        if (index)
          output.push_back(',');
        AppendValue(output, m_enum[index]);
      }
      output.push_back(']');
    });
  }
  if (m_const)
    object.Field("const", [&]() { AppendValue(output, *m_const); });
  if (m_minLength)
    object.Field("minLength", [&]() { output += std::to_string(*m_minLength); });
  if (m_maxLength)
    object.Field("maxLength", [&]() { output += std::to_string(*m_maxLength); });
  if (m_pattern)
    object.Field("pattern", [&]() { AppendString(output, *m_pattern); });
  if (m_format)
    object.Field("format", [&]() { AppendString(output, *m_format); });
  if (m_minimum)
    object.Field("minimum", [&]() { AppendNumber(output, *m_minimum); });
  if (m_maximum)
    object.Field("maximum", [&]() { AppendNumber(output, *m_maximum); });
  if (m_exclusiveMinimum)
    object.Field("exclusiveMinimum", [&]() { AppendNumber(output, *m_exclusiveMinimum); });
  if (m_exclusiveMaximum)
    object.Field("exclusiveMaximum", [&]() { AppendNumber(output, *m_exclusiveMaximum); });
  if (!m_required.empty()) {
    object.Field("required", [&]() {
      output.push_back('[');
      for (size_t index = 0; index < m_required.size(); ++index) {
        if (index)
          output.push_back(',');
        AppendString(output, m_required[index]);
      }
      output.push_back(']');
    });
  }
  if (m_minItems)
    object.Field("minItems", [&]() { output += std::to_string(*m_minItems); });
  if (m_maxItems)
    object.Field("maxItems", [&]() { output += std::to_string(*m_maxItems); });
  if (m_uniqueItems)
    object.Field("uniqueItems", [&]() { output += *m_uniqueItems ? "true" : "false"; });
  object.Finish();
  return output;
}

} // namespace Actions::Json
