#include "NameSearch.hpp"
#include "Context/WireText.hpp"
#include <algorithm>
#include <vector>

namespace Search {
namespace {

std::vector<std::string_view> Words(std::string_view normalized) {
  std::vector<std::string_view> words;
  size_t start = 0;
  while (start < normalized.size()) {
    size_t end = normalized.find(' ', start);
    if (end == std::string_view::npos)
      end = normalized.size();
    if (end > start)
      words.push_back(normalized.substr(start, end - start));
    start = end + 1;
  }
  return words;
}

// Plain Levenshtein distance, stopping early once every cell of a row exceeds `limit`.
size_t EditDistance(std::string_view a, std::string_view b, size_t limit) {
  if (a.size() > b.size() + limit || b.size() > a.size() + limit)
    return limit + 1;
  std::vector<size_t> previous(b.size() + 1);
  std::vector<size_t> current(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j)
    previous[j] = j;
  for (size_t i = 1; i <= a.size(); ++i) {
    current[0] = i;
    size_t rowBest = current[0];
    for (size_t j = 1; j <= b.size(); ++j) {
      const size_t substitution = previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
      current[j] = std::min({previous[j] + 1, current[j - 1] + 1, substitution});
      rowBest = std::min(rowBest, current[j]);
    }
    if (rowBest > limit)
      return limit + 1;
    std::swap(previous, current);
  }
  return previous[b.size()];
}

size_t TypoAllowance(std::string_view word) { return word.size() >= 7 ? 2 : word.size() >= 4 ? 1 : 0; }

// 3 equal, 2 prefix or contained, 1 small typo (against the whole word or the start of it), 0 nothing.
int WordScore(std::string_view queryWord, std::string_view word) {
  if (queryWord == word)
    return 3;
  if (word.starts_with(queryWord) || (queryWord.size() >= 3 && word.find(queryWord) != std::string_view::npos))
    return 2;
  const size_t allowance = TypoAllowance(queryWord);
  if (allowance == 0)
    return 0;
  if (EditDistance(queryWord, word, allowance) <= allowance)
    return 1;
  // A typo in a word still being typed: "mojve" for "mojave", against the word's start of about the same length.
  if (word.size() > queryWord.size() &&
      EditDistance(queryWord, word.substr(0, queryWord.size() + 1), allowance) <= allowance)
    return 1;
  return 0;
}

} // namespace

// Folded to ASCII first, the way Neuro sees names (WireText), so "Taeles" finds the game's "T\xE6les".
std::string Normalize(std::string_view original) {
  const std::string text = WireText::ToAscii(original);
  std::string out;
  out.reserve(text.size());
  bool pendingSpace = false;
  for (const char raw : text) {
    const unsigned char c = static_cast<unsigned char>(raw);
    const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    if (!alnum) {
      pendingSpace = !out.empty();
      continue;
    }
    if (pendingSpace)
      out.push_back(' ');
    pendingSpace = false;
    out.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
  }
  return out;
}

int MatchScore(std::string_view query, std::string_view name, std::string_view alias) {
  const std::string normalizedQuery = Normalize(query);
  const std::string normalizedName = Normalize(name);
  if (normalizedQuery.empty() || normalizedName.empty())
    return 0;
  if (normalizedQuery == normalizedName)
    return 4;

  std::vector<std::string_view> words = Words(normalizedName);
  const std::string normalizedAlias = Normalize(alias);
  for (const auto word : Words(normalizedAlias))
    words.push_back(word);

  int weakest = 3;
  for (const auto queryWord : Words(normalizedQuery)) {
    int best = 0;
    for (const auto word : words)
      best = std::max(best, WordScore(queryWord, word));
    if (best == 0)
      return 0;
    weakest = std::min(weakest, best);
  }
  return weakest;
}

} // namespace Search
