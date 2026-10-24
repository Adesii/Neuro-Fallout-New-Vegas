#include "ObjectPriority.hpp"
#include <algorithm>
#include <cmath>

namespace Objects {

const char *CategoryName(Category category) {
  switch (category) {
  case Category::Actor:
    return "actor";
  case Category::Corpse:
    return "corpse";
  case Category::Activator:
    return "activator";
  case Category::Container:
    return "container";
  case Category::Door:
    return "door";
  case Category::Terminal:
    return "terminal";
  case Category::Harvestable:
    return "harvestable";
  case Category::Furniture:
    return "furniture";
  case Category::Weapon:
    return "weapon";
  case Category::Armor:
    return "armor";
  case Category::Ammo:
    return "ammo";
  case Category::Aid:
    return "aid";
  case Category::Readable:
    return "readable";
  case Category::Loot:
    return "loot";
  case Category::None:
    break;
  }
  return "";
}

bool IsLooseItem(Category category) {
  switch (category) {
  case Category::Weapon:
  case Category::Armor:
  case Category::Ammo:
  case Category::Aid:
  case Category::Readable:
  case Category::Loot:
    return true;
  default:
    return false;
  }
}

int Tier(Category category, bool questTarget) {
  constexpr int kQuestTarget = 1000;
  if (questTarget)
    return kQuestTarget;
  switch (category) {
  case Category::Actor:
    return 900;
  case Category::Container:
    return 700;
  case Category::Terminal:
    return 650;
  case Category::Door:
    return 600;
  case Category::Harvestable:
    return 500;
  case Category::Weapon:
    return 550;
  case Category::Aid:
    return 520;
  case Category::Armor:
    return 450;
  case Category::Ammo:
    return 420;
  case Category::Furniture:
    return 350;
  case Category::Readable:
    return 380;
  case Category::Corpse:
    return 300;
  case Category::Activator:
    return 250;
  case Category::Loot:
    return 100;
  case Category::None:
    break;
  }
  return 0;
}

bool MoreImportant(const Candidate &left, const Candidate &right) {
  const int leftTier = Tier(left.category, left.questTarget);
  const int rightTier = Tier(right.category, right.questTarget);
  if (leftTier != rightTier)
    return leftTier > rightTier;
  if (left.distance != right.distance)
    return left.distance < right.distance;
  return left.formId < right.formId;
}

size_t Rank(std::vector<Candidate> &candidates, size_t limit) {
  const size_t total = candidates.size();
  std::sort(candidates.begin(), candidates.end(), MoreImportant);
  if (candidates.size() > limit)
    candidates.resize(limit);
  return total;
}

std::string TruncationNote(size_t shown, size_t total) {
  if (shown >= total)
    return {};
  return "Showing " + std::to_string(shown) + " of " + std::to_string(total) +
         ", most important first. Move or query again for the rest.";
}

} // namespace Objects
