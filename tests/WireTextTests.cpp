#include "Context/WireText.hpp"
#include "TestFramework.hpp"
#include <string>

using namespace WireText;

namespace {
bool PrintableAscii(const std::string &text) {
  for (const char c : text) {
    const auto byte = static_cast<unsigned char>(c);
    if (byte >= 0x7F || (byte < 0x20 && c != '\n' && c != '\r' && c != '\t'))
      return false;
  }
  return true;
}
} // namespace

TEST("wire text: plain ASCII passes through, with newlines and tabs") {
  CHECK_EQ(ToAscii("## Container contents: Mailbox\n- `1` - Ashtray x1\t!"),
           std::string("## Container contents: Mailbox\n- `1` - Ashtray x1\t!"));
}

TEST("wire text: a Windows-1252 game name is folded (the Taeles of Chivalrie book)") {
  CHECK_EQ(ToAscii("T\xE6les of Chivalrie"), std::string("Taeles of Chivalrie"));
  CHECK_EQ(ToAscii("Caf\xE9 \x93open\x94 \x96 it\x92s \x85"), std::string("Cafe \"open\" - it's ..."));
  CHECK_EQ(ToAscii("Stra\xDF"
                   "e \xA9 \x99"),
           std::string("Strasse (c) (TM)"));
}

TEST("wire text: valid UTF-8 is read as UTF-8, not as Windows-1252") {
  CHECK_EQ(ToAscii("T\xC3\xA6les"), std::string("Taeles"));
  CHECK_EQ(ToAscii("\xE2\x80\x9Cquoted\xE2\x80\x9D \xE2\x80\x94 ok\xE2\x80\xA6"), std::string("\"quoted\" - ok..."));
  CHECK_EQ(ToAscii("\xE6\x97\xA5"), std::string("?")); // a character with no ASCII spelling
  CHECK_EQ(ToAscii("\xF0\x9F\x98\x80"), std::string("?"));
}

TEST("wire text: broken UTF-8 falls back to Windows-1252 byte by byte") {
  CHECK_EQ(ToAscii("\xC3"), std::string("A"));     // a lone lead byte is Ã
  CHECK_EQ(ToAscii("a\xE9z"), std::string("aez")); // é followed by an ASCII byte is not a sequence
  CHECK_EQ(ToAscii("\x81"), std::string("?"));     // undefined in the code page
}

TEST("wire text: control characters other than newline, return and tab are dropped") {
  CHECK_EQ(ToAscii(std::string("a\x01"
                               "b\x1F"
                               "c\x7F"
                               "d",
                               7)),
           std::string("abcd"));
  CHECK_EQ(ToAscii("x\xAD"
                   "y"),
           std::string("xy")); // soft hyphen
  CHECK_EQ(ToAscii(std::string("a\x01"
                               "b",
                               3),
                   true),
           std::string("a\x01"
                       "b",
                       3)); // kept for a JSON writer
}

TEST("wire text: the output is always printable ASCII") {
  std::string every;
  for (int byte = 1; byte < 256; ++byte)
    every.push_back(static_cast<char>(byte));
  CHECK(PrintableAscii(ToAscii(every)));
}

TEST("wire text: a JSON string body escapes quotes, backslashes and line breaks") {
  CHECK_EQ(ToJsonStringBody("Pick \"one\"\\two\nthree\tfour"), std::string("Pick \\\"one\\\"\\\\two\\nthree\\tfour"));
  CHECK_EQ(ToJsonStringBody("\x93hi\x94"), std::string("\\\"hi\\\""));
}
