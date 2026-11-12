#include "Movement/NavGrid.hpp"

#include <algorithm>
#include <cmath>

namespace NavGrid {
namespace {

constexpr float kNoLimit = 1.0e9f;
constexpr int32_t kAxisBias = 1 << 23;  // cell coordinates fit 24 bits: +-268 million units
constexpr int32_t kLevelBias = 1 << 15; // levels fit 16 bits: +-2 million units of height

int32_t FloorDivide(float value, float size) { return static_cast<int32_t>(std::floor(value / size)); }

float Distance2D(const Point3 &a, const Point3 &b) {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  return std::sqrt(dx * dx + dy * dy);
}

} // namespace

Cell CellAt(const Point3 &point) {
  return {FloorDivide(point.x, kCellSize), FloorDivide(point.y, kCellSize), FloorDivide(point.z, kLevelHeight)};
}

Point3 CentreOf(const Cell &cell, float z) {
  return {(static_cast<float>(cell.x) + 0.5f) * kCellSize, (static_cast<float>(cell.y) + 0.5f) * kCellSize, z};
}

uint64_t KeyOf(const Cell &cell) {
  const uint64_t x = static_cast<uint64_t>(static_cast<uint32_t>(cell.x + kAxisBias) & 0xFFFFFFu);
  const uint64_t y = static_cast<uint64_t>(static_cast<uint32_t>(cell.y + kAxisBias) & 0xFFFFFFu);
  const uint64_t level = static_cast<uint64_t>(static_cast<uint32_t>(cell.level + kLevelBias) & 0xFFFFu);
  return (x << 40) | (y << 16) | level;
}

Grid::Node *Grid::Find(const Cell &cell) {
  const auto found = m_nodes.find(KeyOf(cell));
  return found == m_nodes.end() ? nullptr : &found->second;
}

Grid::Node &Grid::Ensure(const Cell &cell, float z) {
  const auto [position, added] = m_nodes.try_emplace(KeyOf(cell));
  if (added)
    position->second.z = z;
  return position->second;
}

void Grid::ForgetTransient() {
  for (auto &[key, node] : m_nodes)
    for (Edge &edge : node.edges)
      if (edge == Edge::Transient)
        edge = Edge::Unknown;
}

void Grid::Block(const Point3 &point, float dirX, float dirY) {
  if (dirX == 0.0f && dirY == 0.0f)
    return;
  // The direction of the eight nearest to (dirX, dirY).
  const float turn = std::atan2(dirY, dirX) / 0.78539816f; // in eighths of a circle
  const int direction = ((static_cast<int>(std::lround(turn)) % 8) + 8) % 8;
  Node &node = Ensure(CellAt(point), point.z);
  for (int offset = -1; offset <= 1; ++offset)
    node.edges[(direction + offset + 8) % 8] = Edge::Blocked;
}

void Search::Reset() { *this = {}; }

float Search::Remaining(const Cell &cell, float z) const { return Distance2D(CentreOf(cell, z), m_goal); }

void Search::Start(Grid &grid, const Point3 &start, const Point3 &goal, const SearchSettings &settings) {
  Reset();
  m_grid = &grid;
  m_settings = settings;
  m_goal = goal;
  grid.ForgetTransient();
  const Cell cell = CellAt(start);
  const Grid::Node &node = grid.Ensure(cell, start.z);
  m_startKey = KeyOf(cell);
  m_visits[m_startKey] = {cell, 0.0f, m_startKey, false};
  m_startRemaining = Remaining(cell, node.z);
  m_nearest = m_startRemaining;
  m_nearestKey = m_startKey;
  m_open.push({m_startRemaining, m_startKey});
  m_state = State::Working;
}

int Search::Evaluate(PathProbe::World &world, const PathProbe::Settings &probe, const Cell &cell, Grid::Node &node,
                     int direction) {
  const Point3 from = CentreOf(cell, node.z);
  Cell next = {cell.x + kDirectionX[direction], cell.y + kDirectionY[direction], cell.level};
  const PathProbe::SweepResult sweep =
      PathProbe::Sweep(world, from, CentreOf(next, node.z), kNoLimit, probe, probe.halfWidth);
  if (sweep.reachedTarget || sweep.blocker == PathProbe::Blocker::Door) {
    node.edges[direction] = Edge::Transient;
  } else if (!sweep.Clear()) {
    node.edges[direction] = Edge::Blocked;
  } else {
    next.level = CellAt(sweep.end).level;
    m_grid->Ensure(next, sweep.end.z);
    node.edges[direction] = Edge::Open;
    node.levels[direction] = next.level;
  }
  return sweep.rays;
}

void Search::Finish(uint64_t endKey, State state) {
  m_path.clear();
  for (uint64_t key = endKey; key != m_startKey;) {
    const Visit &visit = m_visits[key];
    const Grid::Node *node = m_grid->Find(visit.cell);
    m_path.push_back(CentreOf(visit.cell, node ? node->z : 0.0f));
    key = visit.parent;
  }
  std::reverse(m_path.begin(), m_path.end());
  m_state = m_path.empty() ? State::Failed : state;
  m_open = {};
}

Search::State Search::Step(PathProbe::World &world, const PathProbe::Settings &probe, int rayBudget) {
  if (m_state != State::Working)
    return m_state;
  // A cell is only entered when its centre keeps the player's radius from whatever lies beyond it.
  PathProbe::Settings edgeProbe = probe;
  edgeProbe.endClearance = probe.halfWidth;
  int spent = 0;
  while (!m_open.empty()) {
    if (spent >= rayBudget)
      return m_state;
    const uint64_t key = m_open.top().key;
    m_open.pop();
    Visit &visit = m_visits[key];
    if (visit.closed)
      continue; // queued again with a better cost and already expanded
    visit.closed = true;
    ++m_expansions;
    const Cell cell = visit.cell;
    const float cost = visit.cost;
    Grid::Node &node = *m_grid->Find(cell);
    const float remaining = Remaining(cell, node.z);
    const bool atGoalHeight = std::abs(node.z - m_goal.z) <= m_settings.goalHeight;
    if (remaining <= m_settings.goalRadius && atGoalHeight) {
      Finish(key, State::Found);
      return m_state;
    }
    if (remaining < m_nearest && atGoalHeight) {
      m_nearest = remaining;
      m_nearestKey = key;
    }
    if (m_expansions >= m_settings.maxExpansions)
      break;

    // The straight neighbours first; a diagonal only when both straight ones beside it are open, so a route never
    // cuts a corner (and no rays are spent on a diagonal that could not be used).
    int blocked = 0;
    for (int direction : {0, 2, 4, 6, 1, 3, 5, 7}) {
      const bool diagonal = (direction & 1) != 0;
      if (diagonal && (node.edges[(direction + 7) % 8] != Edge::Open || node.edges[(direction + 1) % 8] != Edge::Open))
        continue;
      if (node.edges[direction] == Edge::Unknown) {
        const int rays = Evaluate(world, edgeProbe, cell, node, direction);
        spent += rays;
        m_rays += rays;
      }
      if (node.edges[direction] != Edge::Open)
        ++blocked;
    }
    const float wall = 1.0f + m_settings.wallCost * static_cast<float>(blocked);
    for (int direction = 0; direction < 8; ++direction) {
      if (node.edges[direction] != Edge::Open)
        continue;
      const Cell next = {cell.x + kDirectionX[direction], cell.y + kDirectionY[direction], node.levels[direction]};
      const Grid::Node *nextNode = m_grid->Find(next);
      if (!nextNode)
        continue;
      const float length = (direction & 1) != 0 ? kCellSize * 1.41421356f : kCellSize;
      const float step = (length + m_settings.slopeCost * std::abs(nextNode->z - node.z)) * wall;
      const uint64_t nextKey = KeyOf(next);
      const auto [position, added] = m_visits.try_emplace(nextKey);
      Visit &nextVisit = position->second;
      if (!added && (nextVisit.closed || nextVisit.cost <= cost + step))
        continue;
      nextVisit.cell = next;
      nextVisit.cost = cost + step;
      nextVisit.parent = key;
      m_open.push({nextVisit.cost + Remaining(next, nextNode->z), nextKey});
    }
  }
  // The goal was not reached: out of cells, or out of patience. The nearest cell reached is worth walking to when it
  // is a real step nearer.
  if (m_nearestKey != m_startKey && m_nearest <= m_startRemaining - m_settings.minPartialGain)
    Finish(m_nearestKey, State::Partial);
  else {
    m_state = State::Failed;
    m_open = {};
  }
  return m_state;
}

std::vector<Point3> MergeStraight(const std::vector<Point3> &path) {
  if (path.size() < 3)
    return path;
  std::vector<Point3> merged;
  merged.push_back(path.front());
  for (size_t index = 1; index + 1 < path.size(); ++index) {
    const Point3 &before = merged.back();
    const Point3 &here = path[index];
    const Point3 &after = path[index + 1];
    // Collinear in plan: the cross product of the two legs is zero (cell centres, so exactly or not at all).
    const float cross = (here.x - before.x) * (after.y - here.y) - (here.y - before.y) * (after.x - here.x);
    const float along = (here.x - before.x) * (after.x - here.x) + (here.y - before.y) * (after.y - here.y);
    if (std::abs(cross) > 1.0f || along <= 0.0f)
      merged.push_back(here);
  }
  merged.push_back(path.back());
  return merged;
}

} // namespace NavGrid
