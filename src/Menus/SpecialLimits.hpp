#pragma once

// The rules of the SPECIAL attribute screen, shared by the set_special action and the Vitals Tester executor.
namespace Menus::Special {

inline constexpr int kAttributeCount = 7;
inline constexpr int kMinValue = 1;
inline constexpr int kMaxValue = 10;
// The review page is the one after the last attribute page; page 0 is the cover.
inline constexpr int kLastAttributePage = kAttributeCount;
inline constexpr int kLastPage = kAttributeCount + 1;
// Every attribute at its minimum or maximum bounds any point total the menu can show.
inline constexpr int kMinTotal = kAttributeCount * kMinValue;
inline constexpr int kMaxTotal = kAttributeCount * kMaxValue;

} // namespace Menus::Special
