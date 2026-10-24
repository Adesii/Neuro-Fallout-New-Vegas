#include "Actions/IdList.hpp"
#include "TestFramework.hpp"

using Actions::FormatValidIds;

TEST("valid ids: the list names every id and says how to refresh") {
  CHECK_EQ(FormatValidIds("enemy", {1, 2, 3}, "query_enemies"),
           std::string("Valid enemy ids are 1, 2, 3. Run query_enemies to refresh the list."));
  CHECK_EQ(FormatValidIds("quest", {7}, "query_quests"),
           std::string("Valid quest ids are 7. Run query_quests to refresh the list."));
}

TEST("valid ids: no ids says so and still points at the query") {
  CHECK_EQ(FormatValidIds("nearby object", {}, "query_nearby"),
           std::string("There are no current nearby object ids. Run query_nearby to refresh the list."));
}
