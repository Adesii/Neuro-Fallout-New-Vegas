#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Objects {

// What kind of thing a world reference is, as far as Neuro is concerned. The one classification both `query_nearby`
// (and the "you see" push) and Walker use; the engine side maps form types onto it in WorldUtils::ClassifyObject.
enum class Category {
  None, // not something to list or walk to
  Actor,
  Corpse,
  Activator,
  Container,
  Door,
  Terminal,
  Harvestable,
  Furniture,
  Weapon,
  Armor,
  Ammo,
  Aid,
  Readable,
  Loot,
};

const char *CategoryName(Category category);
// Items lying around (as opposed to things built into the world); an item that is an invisible primitive volume is
// not interactable.
bool IsLooseItem(Category category);

struct Classification {
  Category category = Category::None;
  bool interactable = false;
};

// Importance tier, higher is more important: quest targets first, then living people, containers, terminals, doors,
// weapons, aid, harvestables, armor, ammo, readables, furniture, corpses, activators, and plain loot (clutter) last.
// The numbers are in ObjectPriority.cpp. `questTarget` is any reference an active quest objective points at (or the
// door on the way to one).
int Tier(Category category, bool questTarget);

struct Candidate {
  uint32_t formId = 0;
  Category category = Category::None;
  float distance = 0.0f; // game units
  bool questTarget = false;
};

// Orders by tier (descending), then distance (ascending), then form id, so the same world always gives the same ids.
bool MoreImportant(const Candidate &left, const Candidate &right);

// Sorts `candidates` by MoreImportant and keeps at most `limit`. Returns how many there were before the cut.
size_t Rank(std::vector<Candidate> &candidates, size_t limit);

// "Showing 20 of 57, most important first. Move or query again for the rest." Empty when nothing was cut.
std::string TruncationNote(size_t shown, size_t total);

} // namespace Objects
