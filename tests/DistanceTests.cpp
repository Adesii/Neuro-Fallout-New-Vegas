#include "Context/Distance.hpp"
#include "TestFramework.hpp"
#include <limits>
#include <string>

using namespace Units;

TEST("distance: game units become whole meters at about 70 units per meter") {
  CHECK_EQ(FormatDistance(70.0f), std::string("1 meter"));
  CHECK_EQ(FormatDistance(210.0f), std::string("3 meters"));
  CHECK_EQ(FormatDistance(2000.0f), std::string("29 meters"));
  CHECK_EQ(FormatDistance(5000.0f), std::string("71 meters"));
}

TEST("distance: rounding is to the nearest meter and below one meter it says so") {
  CHECK_EQ(FormatDistance(0.0f), std::string("less than 1 meter"));
  CHECK_EQ(FormatDistance(34.0f), std::string("less than 1 meter"));
  CHECK_EQ(FormatDistance(36.0f), std::string("1 meter"));
  CHECK_EQ(FormatDistance(104.0f), std::string("1 meter"));
  CHECK_EQ(FormatDistance(106.0f), std::string("2 meters"));
}

TEST("distance: nonsense input is reported, not printed") {
  CHECK_EQ(FormatDistance(-1.0f), std::string("an unknown distance"));
  CHECK_EQ(FormatDistance(std::numeric_limits<float>::quiet_NaN()), std::string("an unknown distance"));
  CHECK_EQ(FormatDistance(std::numeric_limits<float>::infinity()), std::string("an unknown distance"));
}

TEST("distance: no message contains the word units") {
  for (float value : {0.0f, 50.0f, 450.0f, 1500.0f, 25000.0f})
    CHECK(FormatDistance(value).find("unit") == std::string::npos);
}
