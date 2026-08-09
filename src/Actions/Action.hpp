#pragma once

#include "Actions/Json/JsonSchema.hpp"
#include <functional>
#include <optional>
#include <string>

namespace Actions {

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
  static PreparedAction Success(std::string message, std::function<void()> execute,
                                std::function<std::optional<std::string>()> revalidate = {});
};

class IAction {
public:
  virtual ~IAction() = default;
  virtual const Definition &GetDefinition() const = 0;
  virtual PreparedAction Validate(const Request &request) = 0;
};

} // namespace Actions
