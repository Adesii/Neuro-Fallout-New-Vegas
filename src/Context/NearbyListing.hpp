#pragma once

#include "Actions/Json/JsonSchema.hpp"
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The engine-free half of `query_nearby`: its optional parameters (`page`, `search`), which ranked entries a page
// shows, and the note under the listing. GameplayHandler scans and ranks the world (Objects::Rank) and feeds the result
// in. Host-tested in tests/NearbyListingTests.cpp.
namespace Nearby {

inline constexpr size_t kPageSize = 20;
inline constexpr int kMaxSearchLength = 64;
inline constexpr std::string_view kPageKey = "page";
inline constexpr std::string_view kSearchKey = "search";

struct Query {
  int page = 1;       // one-based
  std::string search; // empty: no search
};

// Null or empty data, and `{}`, mean page 1 without a search. Unknown fields, a page below 1, a non-string or blank
// search, and a search longer than kMaxSearchLength are refused with a message naming the problem.
std::optional<Query> ParseQuery(std::string_view data, std::string &error);
Actions::Json::JsonSchema BuildQuerySchema();

struct Entry {
  std::string name;
  std::string category; // also searched, so "door" or "container" work
};

// Positions (into `ranked`, which is already in importance order) that the query lists, in display order: with a
// search only the matches, best match first and importance order within the same match quality.
std::vector<size_t> Select(const std::vector<Entry> &ranked, std::string_view search);

// At least one page, also for an empty list.
size_t PageCount(size_t total);
// The slice of `selected` the page shows; empty when the page does not exist.
std::vector<size_t> PageSlice(const std::vector<size_t> &selected, int page);

// State only, no instructions (the action description says how to page and search):
// "Showing 21-38 of 38 (page 2 of 2), most important first." / "Search "box": 3 matches, best match first."
// Empty when everything fits on one page and there is no search.
std::string ListingNote(int page, size_t total, std::string_view search);
// The listing text when `page` is past the end: "Page 3 does not exist: 38 objects fill 2 pages."
std::string MissingPageNote(int page, size_t total, std::string_view search);
// "No nearby object matches "xyz"."
std::string NoMatchNote(std::string_view search);

} // namespace Nearby
