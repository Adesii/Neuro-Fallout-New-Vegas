#include "Action.hpp"
#include <utility>

namespace Actions {

PreparedAction PreparedAction::Failure(std::string message) {
  return {.valid = false, .message = std::move(message), .execute = {}, .revalidate = {}};
}

PreparedAction PreparedAction::Success(std::string message, std::function<void()> execute,
                                       std::function<std::optional<std::string>()> revalidate) {
  return {
      .valid = true, .message = std::move(message), .execute = std::move(execute), .revalidate = std::move(revalidate)};
}

} // namespace Actions
