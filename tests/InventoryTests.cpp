#include "Actions/ActionData.hpp"
#include "Inventory/ItemListing.hpp"
#include "Inventory/ItemQuery.hpp"
#include "TestFramework.hpp"

using namespace Inventory;

namespace {

bool ParseQuery(const std::string &json, int &page, ItemQueryType &type, std::string &error) {
  auto data = Actions::ActionData::Parse(json, error);
  return data && ParseItemQuery(*data, page, type, error);
}

bool ParseIndex(const std::string &json, int &index, std::string &error) {
  auto data = Actions::ActionData::Parse(json, error);
  return data && ParseItemIndex(*data, index, error);
}

ListedItem Item(uint32_t formId, int count) { return {.identity = {formId, 0}, .name = "item", .count = count}; }

} // namespace

TEST("item query: page index with default and explicit type") {
  int page = -1;
  ItemQueryType type = ItemQueryType::Keys;
  std::string error;
  REQUIRE(ParseQuery(R"({"index":0})", page, type, error));
  CHECK_EQ(page, 0);
  CHECK(type == ItemQueryType::All);

  REQUIRE(ParseQuery(R"({"index":3,"type":"Weapons"})", page, type, error));
  CHECK_EQ(page, 3);
  CHECK(type == ItemQueryType::Weapons);
}

TEST("item query: invalid shapes are rejected with an explanation") {
  int page = 0;
  ItemQueryType type = ItemQueryType::All;
  std::string error;
  for (const char *json :
       {R"({})", R"({"index":-1})", R"({"index":"1"})", R"({"index":1,"type":"Bogus"})", R"({"index":1,"type":5})",
        R"({"index":1,"extra":true})", R"([1])", R"({"index":1,"type":"all"})"}) {
    error.clear();
    CHECK(!ParseQuery(json, page, type, error));
    CHECK(!error.empty());
  }
}

TEST("item query: both spellings of the consumable category are accepted") {
  ItemQueryType type = ItemQueryType::All;
  CHECK(ParseItemQueryType("Consumerable", type));
  CHECK(type == ItemQueryType::Consumerable);
  type = ItemQueryType::All;
  CHECK(ParseItemQueryType("Consumable", type));
  CHECK(type == ItemQueryType::Consumerable);
  CHECK(ItemQueryTypeName(ItemQueryType::Consumerable) == "Consumerable");
}

TEST("item query: every category name round trips") {
  for (auto candidate :
       {ItemQueryType::All, ItemQueryType::Weapons, ItemQueryType::Equipment, ItemQueryType::Consumerable,
        ItemQueryType::WeaponMods, ItemQueryType::Keys, ItemQueryType::Misc}) {
    ItemQueryType parsed = ItemQueryType::All;
    CHECK(ParseItemQueryType(ItemQueryTypeName(candidate), parsed));
    CHECK(parsed == candidate);
  }
}

TEST("item query: schema advertises the page index and the category enum") {
  const auto schema = BuildItemQuerySchema().Serialize();
  CHECK(schema.find(R"("index":{"type":"integer","minimum":0})") != std::string::npos);
  CHECK(schema.find(R"("enum":["All","Weapons","Equipment","Consumerable","WeaponMods","Keys","Misc"])") !=
        std::string::npos);
  CHECK(schema.find(R"("required":["index"])") != std::string::npos);
}

TEST("item index: one-based and exact") {
  int index = 0;
  std::string error;
  REQUIRE(ParseIndex(R"({"index":1})", index, error));
  CHECK_EQ(index, 1);
  CHECK(!ParseIndex(R"({"index":0})", index, error));
  CHECK(!ParseIndex(R"({"index":1,"type":"All"})", index, error));
  CHECK(!ParseIndex(R"({"idx":1})", index, error));
  CHECK(BuildItemIndexSchema().Serialize().find(R"("minimum":1)") != std::string::npos);
}

TEST("item listing: unavailable until published, indexes follow the first index") {
  ItemListing listing;
  CHECK(!listing.Available());
  CHECK(listing.Find(1) == nullptr);

  listing.Replace({Item(10, 1), Item(11, 2)}, 16);
  CHECK(listing.Available());
  CHECK(listing.Find(15) == nullptr);
  REQUIRE(listing.Find(16) != nullptr);
  CHECK_EQ(listing.Find(16)->identity.formId, uint32_t{10});
  CHECK_EQ(listing.Find(17)->count, 2);
  CHECK(listing.Find(18) == nullptr);
  CHECK(listing.Find(0) == nullptr);
  CHECK(listing.Find(-3) == nullptr);
}

TEST("item listing: consuming tombstones one index and keeps the others stable") {
  ItemListing listing;
  listing.Replace({Item(10, 1), Item(11, 2)}, 1);
  listing.Consume(1);
  REQUIRE(listing.Find(1) != nullptr);
  CHECK(listing.Find(1)->consumed);
  CHECK(!listing.Find(2)->consumed);
  listing.Consume(99); // out of range is ignored
  listing.Consume(0);
}

TEST("item listing: revisions change on replace and clear so stale actions are detected") {
  ItemListing listing;
  const auto initial = listing.Revision();
  listing.Replace({Item(1, 1)}, 1);
  const auto published = listing.Revision();
  CHECK(published != initial);
  listing.Clear();
  CHECK(listing.Revision() != published);
  CHECK(!listing.Available());
  CHECK(listing.Find(1) == nullptr);
}
