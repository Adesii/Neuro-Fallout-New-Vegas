#include "ActionData.hpp"
#include "json.h"
#include <charconv>
#include <cstdlib>

namespace Actions {

ActionData::ActionData(void *root) : m_root(root) {}

ActionData::ActionData(ActionData &&other) noexcept : m_root(other.m_root) { other.m_root = nullptr; }

ActionData &ActionData::operator=(ActionData &&other) noexcept {
  if (this == &other)
    return *this;
  std::free(m_root);
  m_root = other.m_root;
  other.m_root = nullptr;
  return *this;
}

ActionData::~ActionData() { std::free(m_root); }

std::optional<ActionData> ActionData::Parse(std::string_view source, std::string &error) {
  if (source.empty()) {
    error = "Action data is required.";
    return std::nullopt;
  }
  auto *root = json_parse(source.data(), source.size());
  if (!root) {
    error = "Action data is not valid JSON.";
    return std::nullopt;
  }
  return ActionData(root);
}

bool ActionData::ValidateNoParameters(std::string_view source, std::string &error) {
  if (source.empty())
    return true;
  auto data = Parse(source, error);
  if (!data)
    return false;
  if (!data->IsObject() || data->Size() != 0) {
    error = "This action takes no parameters.";
    return false;
  }
  return true;
}

bool ActionData::IsObject() const {
  auto *root = static_cast<json_value_s *>(m_root);
  return root && root->type == json_type_object;
}

size_t ActionData::Size() const {
  auto *root = static_cast<json_value_s *>(m_root);
  auto *object = root ? json_value_as_object(root) : nullptr;
  return object ? object->length : 0;
}

const void *ActionData::FindElement(std::string_view key) const {
  auto *root = static_cast<json_value_s *>(m_root);
  auto *object = root ? json_value_as_object(root) : nullptr;
  if (!object)
    return nullptr;
  for (auto *element = object->start; element; element = element->next) {
    if (element->name && std::string_view(element->name->string, element->name->string_size) == key)
      return element;
  }
  return nullptr;
}

bool ActionData::HasProperty(std::string_view key) const { return FindElement(key) != nullptr; }

bool ActionData::GetString(std::string_view key, std::string &value) const {
  auto *element = static_cast<const json_object_element_s *>(FindElement(key));
  auto *string = element ? json_value_as_string(element->value) : nullptr;
  if (!string)
    return false;
  value.assign(string->string, string->string_size);
  return true;
}

ActionData::IntegerResult ActionData::ReadInteger(std::string_view key, int &value) const {
  auto *element = static_cast<const json_object_element_s *>(FindElement(key));
  if (!element)
    return IntegerResult::Missing;
  auto *number = json_value_as_number(element->value);
  if (!number)
    return IntegerResult::NotAnInteger;
  const char *end = number->number + number->number_size;
  int parsed = 0;
  const auto result = std::from_chars(number->number, end, parsed);
  if (result.ec == std::errc::result_out_of_range)
    return IntegerResult::OutOfRange;
  // 1.5 and 1e2 stop short of the end: JSON numbers that are not written as integers are refused.
  if (result.ec != std::errc() || result.ptr != end)
    return IntegerResult::NotAnInteger;
  value = parsed;
  return IntegerResult::Ok;
}

bool ActionData::GetPositiveInteger(std::string_view key, int &value, std::string &error) const {
  const std::string name(key);
  int parsed = 0;
  switch (ReadInteger(key, parsed)) {
  case IntegerResult::Missing:
    error = name + " is required.";
    return false;
  case IntegerResult::NotAnInteger:
    error = name + " must be an integer.";
    return false;
  case IntegerResult::OutOfRange:
    error = name + " is too large.";
    return false;
  case IntegerResult::Ok:
    break;
  }
  if (parsed < 1) {
    error = name + " must be a positive integer.";
    return false;
  }
  value = parsed;
  return true;
}

bool ActionData::ParsePositiveId(std::string_view source, std::string_view key, int &id, std::string &error) {
  auto data = Parse(source, error);
  if (!data)
    return false;
  if (!data->IsObject() || data->Size() != 1 || !data->HasProperty(key)) {
    error = "Expected exactly one integer property named " + std::string(key) + ".";
    return false;
  }
  return data->GetPositiveInteger(key, id, error);
}

} // namespace Actions
