#pragma once

#include <string>
#include <string_view>

// Text that leaves the plugin for Neuro must be plain printable ASCII. The pinned libneurosdk (`src/neurosdk.c`,
// escape_string) writes every byte outside 32..126 other than \n, \r and \t as `\xNN`, which is not JSON, so the
// server drops the whole message ("Invalid escape character x"; seen with the book "T\xE6les of Chivalrie" in a
// container listing, 2026-11-08). It also inserts an actions/force `query` without escaping it at all.
//
// Game strings are Windows-1252 (the English game's code page; an assumption, see todo 40's notes); strings that are
// valid UTF-8 (Neuro's own text, character names from the SDK) are read as UTF-8. Each character is folded to the
// nearest ASCII spelling ("ae" for æ, straight quotes for curly ones), the rest becomes '?'. Engine-free, tested in
// tests/WireTextTests.cpp.
namespace WireText {

// Printable ASCII plus \n, \r and \t; other control characters are dropped, or kept with `keepControls` (for a JSON
// writer that escapes them itself).
std::string ToAscii(std::string_view text, bool keepControls = false);

// ToAscii, then escaped as the inside of a JSON string literal (quotes, backslashes, \n, \r, \t), for fields that the
// transport inserts into its JSON without escaping (the force query).
std::string ToJsonStringBody(std::string_view text);

} // namespace WireText
