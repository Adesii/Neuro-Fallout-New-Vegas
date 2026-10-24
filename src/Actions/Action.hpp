#pragma once

#include "Actions/Json/JsonSchema.hpp"
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace Actions {

// Project action names: lowercase letters and underscores, no leading/trailing underscore.
inline bool IsValidActionName(const std::string &name) {
  if (name.empty() || name.front() == '_' || name.back() == '_')
    return false;
  for (char character : name) {
    if ((character < 'a' || character > 'z') && character != '_')
      return false;
  }
  return true;
}

struct Definition {
  std::string name;
  std::string description;
  Json::JsonSchema schema;
};

struct Request {
  std::string id;
  std::string name;
  std::string data;
};

struct PreparedAction {
  bool valid = false;
  std::string message;
  std::function<void()> execute;
  std::function<std::optional<std::string>()> revalidate;

  static PreparedAction Failure(std::string message);
  static PreparedAction Success(std::function<void()> execute,
                                std::function<std::optional<std::string>()> revalidate = {});
};

// Turns "is this still valid, and if not why" (`check(error)` returns true while valid) into the revalidation callback
// of a prepared action.
template <class Check> std::function<std::optional<std::string>()> Revalidator(Check check) {
  return [check = std::move(check)]() -> std::optional<std::string> {
    std::string error;
    if (!check(error))
      return error;
    return std::nullopt;
  };
}

class IAction {
public:
  virtual ~IAction() = default;
  virtual const Definition &GetDefinition() const = 0;
  virtual PreparedAction Validate(const Request &request) = 0;
};

} // namespace Actions
