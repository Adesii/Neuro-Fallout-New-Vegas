#include "StowAwayItemAction.hpp"
#include "Actions/ActionData.hpp"
#include "Menus/ContainerHandler.hpp"
#include <utility>

namespace Actions::Container {
namespace {

const Definition kDefinition = {
    .name = "stow_away_item",
    .description = "Stow the entire listed stack using its one-based stable index from the last own-items listing. "
                   "Query your items first to obtain current indexes.",
    .schema = [] {
      auto schema = Json::JsonSchema::Object();
      auto index = Json::JsonSchema::Integer();
      index.Minimum(1);
      schema.Property("index", std::move(index));
      return schema;
    }()};

} // namespace

const Definition &StowAwayItemAction::GetDefinition() const { return kDefinition; }

PreparedAction StowAwayItemAction::Validate(const Request &request) {
  std::string error;
  auto data = ActionData::Parse(request.data, error);
  if (!data)
    return PreparedAction::Failure(std::move(error));
  int index = 0;
  if (!data->IsObject() || data->Size() != 1 || !data->HasProperty("index") || !data->GetInteger("index", index))
    return PreparedAction::Failure("Expected exactly one integer field: index.");
  if (index < 1)
    return PreparedAction::Failure("index must be a one-based item index of at least 1.");
  return Menus::ContainerHandler::PrepareTransfer(true, index);
}

} // namespace Actions::Container
