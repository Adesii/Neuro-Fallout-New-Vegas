#include "NearbyListing.hpp"
#include "Actions/ActionData.hpp"
#include "Context/NameSearch.hpp"
#include <algorithm>

namespace Nearby {

std::optional<Query> ParseQuery(std::string_view data, std::string &error) {
  Query query;
  if (data.empty())
    return query;
  auto parsed = Actions::ActionData::Parse(data, error);
  if (!parsed)
    return std::nullopt;
  if (!parsed->IsObject()) {
    error = "Expected an object with an optional page and an optional search.";
    return std::nullopt;
  }

  size_t known = 0;
  if (parsed->HasProperty(kPageKey)) {
    ++known;
    if (!parsed->GetPositiveInteger(kPageKey, query.page, error))
      return std::nullopt;
  }
  if (parsed->HasProperty(kSearchKey)) {
    ++known;
    if (!parsed->GetString(kSearchKey, query.search)) {
      error = "search must be a string, for example the name of what you are looking for.";
      return std::nullopt;
    }
    if (query.search.size() > static_cast<size_t>(kMaxSearchLength)) {
      error = "search is too long (at most " + std::to_string(kMaxSearchLength) + " characters).";
      return std::nullopt;
    }
    if (Search::Normalize(query.search).empty()) {
      error = "search must contain at least one letter or digit.";
      return std::nullopt;
    }
  }
  if (parsed->Size() != known) {
    error = "Only page and search are allowed; unexpected or duplicate fields are not.";
    return std::nullopt;
  }
  return query;
}

Actions::Json::JsonSchema BuildQuerySchema() {
  auto schema = Actions::Json::JsonSchema::Object();
  auto page = Actions::Json::JsonSchema::Integer();
  page.Minimum(1);
  schema.Property(std::string(kPageKey), std::move(page), false);
  auto search = Actions::Json::JsonSchema::String();
  search.MinLength(1).MaxLength(kMaxSearchLength);
  schema.Property(std::string(kSearchKey), std::move(search), false);
  return schema;
}

std::vector<size_t> Select(const std::vector<Entry> &ranked, std::string_view search) {
  std::vector<size_t> selected;
  if (Search::Normalize(search).empty()) {
    selected.reserve(ranked.size());
    for (size_t index = 0; index < ranked.size(); ++index)
      selected.push_back(index);
    return selected;
  }
  std::vector<int> scores(ranked.size(), 0);
  for (size_t index = 0; index < ranked.size(); ++index) {
    scores[index] = Search::MatchScore(search, ranked[index].name, ranked[index].category);
    if (scores[index] > 0)
      selected.push_back(index);
  }
  std::stable_sort(selected.begin(), selected.end(),
                   [&](size_t left, size_t right) { return scores[left] > scores[right]; });
  return selected;
}

size_t PageCount(size_t total) { return total == 0 ? 1 : (total + kPageSize - 1) / kPageSize; }

std::vector<size_t> PageSlice(const std::vector<size_t> &selected, int page) {
  if (page < 1 || static_cast<size_t>(page) > PageCount(selected.size()))
    return {};
  const size_t first = (static_cast<size_t>(page) - 1) * kPageSize;
  const size_t last = std::min(first + kPageSize, selected.size());
  return {selected.begin() + static_cast<std::ptrdiff_t>(first), selected.begin() + static_cast<std::ptrdiff_t>(last)};
}

namespace {

std::string Quoted(std::string_view search) { return "\"" + std::string(search) + "\""; }

std::string PageRange(int page, size_t total) {
  const size_t first = (static_cast<size_t>(page) - 1) * kPageSize + 1;
  const size_t last = std::min(first + kPageSize - 1, total);
  return "Showing " + std::to_string(first) + "-" + std::to_string(last) + " of " + std::to_string(total) + " (page " +
         std::to_string(page) + " of " + std::to_string(PageCount(total)) + ")";
}

} // namespace

std::string ListingNote(int page, size_t total, std::string_view search) {
  const bool searching = !Search::Normalize(search).empty();
  const bool paged = total > kPageSize;
  if (!searching && !paged)
    return {};
  std::string note;
  if (searching)
    note = "Search " + Quoted(search) + ": " + std::to_string(total) + (total == 1 ? " match" : " matches") +
           ", best match first.";
  if (paged) {
    if (!note.empty())
      note += " ";
    note += PageRange(page, total) + (searching ? "." : ", most important first.");
  }
  return note;
}

std::string MissingPageNote(int page, size_t total, std::string_view search) {
  const bool searching = !Search::Normalize(search).empty();
  const size_t pages = PageCount(total);
  return "Page " + std::to_string(page) + " does not exist: " + std::to_string(total) +
         (searching ? (total == 1 ? " match for " : " matches for ") + Quoted(search)
                    : std::string(total == 1 ? " object" : " objects")) +
         (pages == 1 ? " fill 1 page." : " fill " + std::to_string(pages) + " pages.");
}

std::string NoMatchNote(std::string_view search) { return "No nearby object matches " + Quoted(search) + "."; }

} // namespace Nearby
