#include "Context/WireText.hpp"

#include <cstdint>
#include <vector>

namespace WireText {
namespace {

// Windows-1252 bytes 0x80..0x9F as Unicode code points; 0 where the code page leaves the byte undefined.
constexpr uint16_t kCp1252High[32] = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0,      0x017D, 0,      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178,
};

// ASCII spellings of U+00A0..U+00FF (Latin-1 Supplement). Empty: dropped (soft hyphen and lone accents).
constexpr const char *kLatin1[96] = {
    " ",    "!",   "c", "L", "?", "Y", "|",  "S", "",  "(c)", "a", "<<", "-",   "",    "(R)", "",
    " deg", "+/-", "2", "3", "",  "u", "P",  ".", "",  "1",   "o", ">>", "1/4", "1/2", "3/4", "?",
    "A",    "A",   "A", "A", "A", "A", "AE", "C", "E", "E",   "E", "E",  "I",   "I",   "I",   "I",
    "D",    "N",   "O", "O", "O", "O", "O",  "x", "O", "U",   "U", "U",  "U",   "Y",   "TH",  "ss",
    "a",    "a",   "a", "a", "a", "a", "ae", "c", "e", "e",   "e", "e",  "i",   "i",   "i",   "i",
    "d",    "n",   "o", "o", "o", "o", "o",  "/", "o", "u",   "u", "u",  "u",   "y",   "th",  "y",
};

const char *FoldOther(uint32_t cp) {
  switch (cp) {
  case 0x0152:
    return "OE";
  case 0x0153:
    return "oe";
  case 0x0160:
    return "S";
  case 0x0161:
    return "s";
  case 0x0178:
    return "Y";
  case 0x017D:
    return "Z";
  case 0x017E:
    return "z";
  case 0x0192:
    return "f";
  case 0x02C6:
    return "^";
  case 0x02DC:
    return "~";
  case 0x2010:
  case 0x2011:
  case 0x2012:
  case 0x2013:
  case 0x2014:
  case 0x2015:
  case 0x2212:
    return "-";
  case 0x2018:
  case 0x2019:
  case 0x201A:
  case 0x201B:
  case 0x2032:
  case 0x2039:
  case 0x203A:
    return cp == 0x2039 ? "<" : cp == 0x203A ? ">" : "'";
  case 0x201C:
  case 0x201D:
  case 0x201E:
  case 0x201F:
  case 0x2033:
    return "\"";
  case 0x2020:
    return "+";
  case 0x2021:
    return "++";
  case 0x2022:
  case 0x00B7:
    return "*";
  case 0x2026:
    return "...";
  case 0x2030:
    return " per mille";
  case 0x20AC:
    return "EUR";
  case 0x2122:
    return "(TM)";
  case 0x2002:
  case 0x2003:
  case 0x2009:
  case 0x202F:
    return " ";
  case 0x200B:
  case 0xFEFF:
    return "";
  default:
    return "?";
  }
}

void Fold(uint32_t cp, std::string &out, bool keepControls) {
  if (cp < 0x80) {
    const bool printable = cp >= 0x20 && cp < 0x7F;
    if (printable || cp == '\n' || cp == '\r' || cp == '\t' || (keepControls && cp < 0x20))
      out.push_back(static_cast<char>(cp));
    return; // other control characters carry nothing for Neuro
  }
  if (cp < 0xA0)
    return; // C1 controls
  if (cp <= 0xFF) {
    out += kLatin1[cp - 0xA0];
    return;
  }
  out += FoldOther(cp);
}

// The code points of `text` if it is entirely valid UTF-8 (shortest forms, no surrogates), else false.
bool DecodeUtf8(std::string_view text, std::vector<uint32_t> &out) {
  out.clear();
  for (size_t i = 0; i < text.size();) {
    const auto lead = static_cast<unsigned char>(text[i]);
    int extra = 0;
    uint32_t cp = 0;
    if (lead < 0x80) {
      cp = lead;
    } else if (lead >= 0xC2 && lead <= 0xDF) {
      extra = 1;
      cp = lead & 0x1F;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
      extra = 2;
      cp = lead & 0x0F;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
      extra = 3;
      cp = lead & 0x07;
    } else {
      return false;
    }
    if (i + static_cast<size_t>(extra) >= text.size())
      return false; // the sequence runs past the end
    for (int k = 1; k <= extra; ++k) {
      const auto next = static_cast<unsigned char>(text[i + k]);
      if ((next & 0xC0) != 0x80)
        return false;
      cp = (cp << 6) | (next & 0x3F);
    }
    if ((extra == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) ||
        (extra == 3 && (cp < 0x10000 || cp > 0x10FFFF)))
      return false;
    out.push_back(cp);
    i += static_cast<size_t>(extra) + 1;
  }
  return true;
}

} // namespace

std::string ToAscii(std::string_view text, bool keepControls) {
  std::string out;
  out.reserve(text.size());
  bool ascii = true;
  for (const char c : text)
    ascii = ascii && static_cast<unsigned char>(c) < 0x80;
  if (ascii) {
    for (const char c : text)
      Fold(static_cast<unsigned char>(c), out, keepControls);
    return out;
  }
  std::vector<uint32_t> codePoints;
  if (DecodeUtf8(text, codePoints)) {
    for (const uint32_t cp : codePoints)
      Fold(cp, out, keepControls);
    return out;
  }
  for (const char c : text) {
    const auto byte = static_cast<unsigned char>(c);
    if (byte >= 0x80 && byte < 0xA0) {
      const uint16_t cp = kCp1252High[byte - 0x80];
      if (cp)
        Fold(cp, out, keepControls);
      else
        out.push_back('?');
    } else {
      Fold(byte, out, keepControls);
    }
  }
  return out;
}

std::string ToJsonStringBody(std::string_view text) {
  const std::string ascii = ToAscii(text);
  std::string out;
  out.reserve(ascii.size() + 8);
  for (const char c : ascii) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out.push_back(c);
    }
  }
  return out;
}

} // namespace WireText
