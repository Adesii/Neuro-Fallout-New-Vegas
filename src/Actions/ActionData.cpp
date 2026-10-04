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

bool ActionData::HasProperty(std::string_view key) const {
  auto *root = static_cast<json_value_s *>(m_root);
  auto *object = root ? json_value_as_object(root) : nullptr;
  if (!object)
    return false;
  for (auto *element = object->start; element; element = element->next) {
    if (element->name && element->name->string_size == key.size() &&
        std::string_view(element->name->string, element->name->string_size) == key)
      return true;
  }
  return false;
}

bool ActionData::GetString(std::string_view key, std::string &value) const {
  auto *root = static_cast<json_value_s *>(m_root);
  auto *object = root ? json_value_as_object(root) : nullptr;
  if (!object)
    return false;

  for (auto *element = object->start; element; element = element->next) {
    if (!element->name || element->name->string_size != key.size() ||
        std::string_view(element->name->string, element->name->string_size) != key)
      continue;
    auto *string = json_value_as_string(element->value);
    if (!string)
      return false;
    value.assign(string->string, string->string_size);
    return true;
  }
  return false;
}

bool ActionData::GetInteger(std::string_view key, int &value) const {
  auto *root = static_cast<json_value_s *>(m_root);
  auto *object = root ? json_value_as_object(root) : nullptr;
  if (!object)
    return false;

  for (auto *element = object->start; element; element = element->next) {
    if (!element->name || element->name->string_size != key.size() ||
        std::string_view(element->name->string, element->name->string_size) != key)
      continue;
    auto *number = json_value_as_number(element->value);
    if (!number)
      return false;
    const char *end = number->number + number->number_size;
    auto result = std::from_chars(number->number, end, value);
    return result.ec == std::errc() && result.ptr == end;
  }
  return false;
}

} // namespace Actions
