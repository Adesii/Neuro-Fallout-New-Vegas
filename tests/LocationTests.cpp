#include "Context/Location.hpp"
#include "TestFramework.hpp"
#include <limits>
#include <utility>

using namespace Location;

namespace {

PlaceInput Outdoors(std::vector<Marker> markers = {}, uint32_t space = 1) {
  PlaceInput input;
  input.spaceId = space;
  input.markers = std::move(markers);
  return input;
}

PlaceInput Named(const char *name, bool interior, uint32_t space) {
  PlaceInput input;
  input.interior = interior;
  input.spaceId = space;
  input.cellName = name;
  return input;
}

// Feeds the same observation for `seconds` in half-second steps and returns the first line.
std::optional<std::string> Hold(PlaceTracker &tracker, const PlaceInput &input, float seconds) {
  for (float t = 0.0f; t < seconds; t += 0.5f) {
    if (auto line = tracker.Update(input, 0.5f))
      return line;
  }
  return std::nullopt;
}

} // namespace

TEST("place: a named cell wins over any marker") {
  PlaceInput input = Named("Goodsprings Saloon", true, 7);
  input.markers = {{"Goodsprings", 10.0f}};
  CHECK_EQ(DescribePlace(input), std::string("in Goodsprings Saloon"));
  PlaceInput exterior = Named("Goodsprings", false, 1);
  exterior.markers = {{"Prospector Saloon", 10.0f}};
  CHECK_EQ(DescribePlace(exterior), std::string("in Goodsprings"));
}

TEST("place: no cell name picks the nearest marker, ties by name, farther than the limit is ignored") {
  CHECK_EQ(DescribePlace(Outdoors({{"Far Ruins", 9000.0f}, {"Goodsprings", 4000.0f}})),
           std::string("near Goodsprings"));
  CHECK_EQ(DescribePlace(Outdoors({{"B", 5.0f}, {"A", 5.0f}})), std::string("near A"));
  CHECK_EQ(DescribePlace(Outdoors({{"Faraway", kNearMarkerUnits + 1.0f}})), std::string("in the wasteland"));
  CHECK_EQ(DescribePlace(Outdoors({{"Edge", kNearMarkerUnits}})), std::string("near Edge"));
}

TEST("place: no markers, unnamed markers and nan distances fall back to the wasteland; unnamed interiors say so") {
  CHECK_EQ(DescribePlace(Outdoors()), std::string("in the wasteland"));
  CHECK_EQ(DescribePlace(Outdoors({{"", 5.0f}})), std::string("in the wasteland"));
  CHECK_EQ(DescribePlace(Outdoors({{"Nan", std::numeric_limits<float>::quiet_NaN()}})),
           std::string("in the wasteland"));
  PlaceInput interior;
  interior.interior = true;
  interior.spaceId = 4;
  CHECK_EQ(DescribePlace(interior), std::string("in an interior area"));
}

TEST("place tracker: the first stable place is announced once, not before it is stable") {
  PlaceTracker tracker;
  const auto input = Named("Goodsprings", false, 1);
  CHECK(!tracker.Update(input, 0.5f)); // first sighting starts the stability clock
  CHECK(!tracker.Update(input, 1.0f));
  auto line = tracker.Update(input, 1.0f);
  REQUIRE(line.has_value());
  CHECK_EQ(*line, std::string("You are now in Goodsprings."));
  CHECK(!Hold(tracker, input, 60.0f)); // nothing repeats
  CHECK_EQ(tracker.Announced(), std::string("in Goodsprings"));
}

TEST("place tracker: a flickering observation (a load screen, a cell crossing) announces nothing") {
  PlaceTracker tracker;
  const auto a = Named("Goodsprings", false, 1);
  const auto b = Outdoors();
  for (int i = 0; i < 40; ++i)
    CHECK(!tracker.Update(i % 2 ? a : b, 1.0f));
}

TEST("place tracker: walking through a door announces the new space at once when stable") {
  PlaceTracker tracker;
  REQUIRE(Hold(tracker, Named("Goodsprings", false, 1), 5.0f).has_value());
  auto line = Hold(tracker, Named("Goodsprings Saloon", true, 9), 5.0f);
  REQUIRE(line.has_value());
  CHECK_EQ(*line, std::string("You are now in Goodsprings Saloon."));
  line = Hold(tracker, Named("Goodsprings", false, 1), 5.0f); // right back out: a different space, so immediately
  REQUIRE(line.has_value());
}

TEST("place tracker: moving between outdoor descriptions inside one space waits for the cooldown") {
  PlaceTracker tracker;
  REQUIRE(Hold(tracker, Outdoors({{"Goodsprings", 100.0f}}), 5.0f).has_value());
  const auto elsewhere = Outdoors({{"Primm", 100.0f}});
  CHECK(!Hold(tracker, elsewhere, 20.0f)); // under the 30 s cooldown
  auto line = Hold(tracker, elsewhere, 20.0f);
  REQUIRE(line.has_value());
  CHECK_EQ(*line, std::string("You are now near Primm."));
}

TEST("place tracker: reset makes the next stable place announce again (a load)") {
  PlaceTracker tracker;
  const auto input = Named("Goodsprings", false, 1);
  REQUIRE(Hold(tracker, input, 5.0f).has_value());
  tracker.Reset();
  CHECK(tracker.Announced().empty());
  CHECK(Hold(tracker, input, 5.0f).has_value());
}
