#pragma once

#include <string>
#include <string_view>

// Forgiving name search for Neuro's free-text queries (`query_nearby` search). Engine-free, host-tested in
// tests/NameSearchTests.cpp. Case, punctuation and word order do not matter, a word may be typed partly ("mojave"
// finds "Mojave Express Dropbox") or with a small typo ("mailbx"), and every word of the query has to match something.
namespace Search {

// Lowercase ASCII letters and digits, every other run of characters collapsed into one space, no leading or trailing
// space.
std::string Normalize(std::string_view text);

// How well `query` matches `name`, higher is better, 0 when it does not match. `alias` is extra text that may also
// satisfy a word (the object's category, so "container" or "door" work), but only `name` can give an exact match.
//   4  the whole name, ignoring case and punctuation
//   3  every query word equals a word
//   2  every query word starts or is contained in a word (3+ letters)
//   1  every query word matches some word with a small typo (1 edit from 4 letters on, 2 from 7)
// A query word's own best way of matching counts; the weakest word decides the score.
int MatchScore(std::string_view query, std::string_view name, std::string_view alias = {});

} // namespace Search
