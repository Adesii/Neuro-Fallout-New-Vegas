#pragma once

#include "Actions/ActionData.hpp"
#include "Actions/Json/JsonSchema.hpp"
#include <string>
#include <string_view>

class TESForm;

namespace Inventory {

enum class ItemQueryType { All, Weapons, Equipment, Consumerable, WeaponMods, Keys, Misc };

bool ParseItemQueryType(std::string_view name, ItemQueryType &type);
std::string_view ItemQueryTypeName(ItemQueryType type);
bool ParseItemQuery(const Actions::ActionData &data, int &page, ItemQueryType &type, std::string &error);
Actions::Json::JsonSchema BuildItemQuerySchema();
ItemQueryType ClassifyItem(const TESForm &form);
bool MatchesQuery(const TESForm *form, ItemQueryType type);

} // namespace Inventory
