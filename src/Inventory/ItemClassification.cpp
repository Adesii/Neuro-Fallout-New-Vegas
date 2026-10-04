#include "Bethesda/TESForm.hpp"
#include "Inventory/ItemQuery.hpp"

namespace Inventory {

ItemQueryType ClassifyItem(const TESForm &form) {
  switch (form.eFormType) {
  case _FormType::TESObjectWEAP:
  case _FormType::TESAmmo:
    return ItemQueryType::Weapons;
  case _FormType::TESObjectARMO:
  case _FormType::TESObjectCLOT:
    return ItemQueryType::Equipment;
  case _FormType::AlchemyItem:
  case _FormType::IngredientItem:
    return ItemQueryType::Consumerable;
  case _FormType::TESObjectIMOD:
    return ItemQueryType::WeaponMods;
  case _FormType::TESKey:
    return ItemQueryType::Keys;
  default:
    // Books, notes, junk, crafting components, currency and other inventory forms.
    return ItemQueryType::Misc;
  }
}

bool MatchesQuery(const TESForm *form, ItemQueryType type) {
  return form && (type == ItemQueryType::All || ClassifyItem(*form) == type);
}

} // namespace Inventory
