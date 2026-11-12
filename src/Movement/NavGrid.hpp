#pragma once

// A navigation grid made from rays (todo 45), for the places where the game's navmesh is wrong, missing, or does not
// know an obstacle. The world is cut into square cells; whether the player can walk from one cell to the next is
// asked of PathProbe::Sweep (the same floor-following ray check the Walker's path check uses), and A* searches over
// the answers. Nothing is sampled ahead of time: a search samples only the cells it reaches, and the grid remembers
// every answer, so the second search over the same ground costs no rays.
//
// Engine-free: the rays come through PathProbe::World. WalkerHandler owns one Grid per interior or worldspace and
// runs searches a ray budget at a time. Design, numbers and what is unverified: docs/design/custom-navigation-grid.md.

#include "Movement/PathProbe.hpp"
#include <cstdint>
#include <queue>
#include <unordered_map>
#include <vector>

namespace NavGrid {

using PathProbe::Point3;

// Cells are aligned to the world, so two searches (and a saved grid, later) agree on them. 32 units keeps a ray
// within the player's collision radius (20.3) of every point between two neighbouring lines, so nothing as wide as
// the player fits through unseen; a gap has to be about 73 units wide to be sure of holding a line.
inline constexpr float kCellSize = 32.0f;
// Floors above each other (a bridge over a road, the storeys of a building) are different cells: the level is the
// floor's height in steps of this. Two floors the player can stand on are further apart than this.
inline constexpr float kLevelHeight = 64.0f;

struct Cell {
  int32_t x = 0;
  int32_t y = 0;
  int32_t level = 0;
  bool operator==(const Cell &other) const = default;
};

Cell CellAt(const Point3 &point);
// The centre of the cell, at floor height `z`.
Point3 CentreOf(const Cell &cell, float z);
uint64_t KeyOf(const Cell &cell);

// The eight neighbours, counter-clockwise from +x: even numbers are straight, odd ones diagonal.
inline constexpr int kDirectionX[8] = {1, 1, 0, -1, -1, -1, 0, 1};
inline constexpr int kDirectionY[8] = {0, 1, 1, 1, 0, -1, -1, -1};

enum class Edge : uint8_t {
  Unknown,
  Open,
  Blocked,
  // Blocked for now by something that changes (a door, the walk's own target): every new search asks again.
  Transient,
};

// What has been sampled so far. Lives as long as the player stays in one interior or worldspace.
class Grid {
public:
  struct Node {
    float z = 0.0f;         // floor height at the cell's centre
    Edge edges[8] = {};     // whether the player can walk to each neighbour
    int32_t levels[8] = {}; // Open: the level of the neighbour reached
  };

  void Clear() { m_nodes.clear(); }
  size_t NodeCount() const { return m_nodes.size(); }
  Node *Find(const Cell &cell);
  // The node of `cell`, made with floor height `z` when it does not exist yet.
  Node &Ensure(const Cell &cell, float z);
  // Forgets the Transient answers (at the start of every search).
  void ForgetTransient();
  // The player got stuck at `point` trying to go towards (dirX, dirY): the edge that way and the two beside it are
  // blocked from now on, whatever the rays said.
  void Block(const Point3 &point, float dirX, float dirY);

private:
  std::unordered_map<uint64_t, Node> m_nodes;
};

struct SearchSettings {
  // Cells a search may expand before it gives up on reaching the goal: about 29 m x 29 m of ground, and at about 70
  // rays a cell some 280,000 rays, 1.4 s of ray time spread over the frames.
  int maxExpansions = 4000;
  // The goal is reached within this distance of it, at about its height.
  float goalRadius = 48.0f;
  float goalHeight = 100.0f;
  // Climbing costs extra: this much per unit of height.
  float slopeCost = 1.0f;
  // Leaving a cell that has blocked edges costs this much more per blocked edge, so routes keep off walls a little.
  float wallCost = 0.15f;
  // A search that does not reach the goal still gives a route to the cell nearest to it, when that is at least this
  // much nearer than the start.
  float minPartialGain = 100.0f;
};

// One A* search from a start to a goal, cut into calls by a ray budget.
class Search {
public:
  enum class State {
    Idle,
    Working,
    Found,   // a route to the goal
    Partial, // the goal was not reached; a route to the nearest cell that was
    Failed,  // nowhere to go that is nearer the goal
  };

  // `grid` must outlive the search. `start` is where the player stands (a floor point).
  void Start(Grid &grid, const Point3 &start, const Point3 &goal, const SearchSettings &settings = {});
  void Reset();
  // Carries on until done or until `rayBudget` rays were cast in this call (one cell's edges are never cut short).
  State Step(PathProbe::World &world, const PathProbe::Settings &probe, int rayBudget);
  State GetState() const { return m_state; }
  // Found, Partial: the centres of the cells to walk through, on their floors, without the start cell.
  const std::vector<Point3> &Path() const { return m_path; }
  int Rays() const { return m_rays; }
  int Expansions() const { return m_expansions; }

private:
  struct Visit {
    Cell cell;
    float cost = 0.0f;
    uint64_t parent = 0;
    bool closed = false;
  };
  struct Queued {
    float priority;
    uint64_t key;
    bool operator<(const Queued &other) const { return priority > other.priority; } // smallest first
  };

  int Evaluate(PathProbe::World &world, const PathProbe::Settings &probe, const Cell &cell, Grid::Node &node,
               int direction);
  float Remaining(const Cell &cell, float z) const;
  void Finish(uint64_t endKey, State state);

  State m_state = State::Idle;
  Grid *m_grid = nullptr;
  SearchSettings m_settings;
  Point3 m_goal;
  uint64_t m_startKey = 0;
  uint64_t m_nearestKey = 0;
  float m_nearest = 0.0f;
  float m_startRemaining = 0.0f;
  std::unordered_map<uint64_t, Visit> m_visits;
  std::priority_queue<Queued> m_open;
  std::vector<Point3> m_path;
  int m_rays = 0;
  int m_expansions = 0;
};

// Drops the waypoints in the middle of a straight run: the cells of a run were each checked on that same line.
std::vector<Point3> MergeStraight(const std::vector<Point3> &path);

} // namespace NavGrid
