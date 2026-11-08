#pragma once

#include "Actions/Action.hpp"

namespace Actions::Gameplay {

// `search_nearby`: query_nearby's listing filtered by a required name or kind, best match first (todo 46 split it out
// of query_nearby's optional search on 2026-11-08). The ids are the same as query_nearby's.
class SearchNearbyAction final : public IAction {
public:
  const Definition &GetDefinition() const override;
  PreparedAction Validate(const Request &request) override;
};

} // namespace Actions::Gameplay
