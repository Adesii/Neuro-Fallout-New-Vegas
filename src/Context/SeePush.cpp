#include "SeePush.hpp"
#include "Context/Distance.hpp"
#include <algorithm>

namespace Context {

bool IsNotable(Objects::Category category, bool questTarget) {
  return questTarget || category == Objects::Category::Container || category == Objects::Category::Actor;
}

void SeePush::Reset() { m_seen.clear(); }

void SeePush::ResetAll() {
  m_seen.clear();
  m_sinceLastPush = kMinInterval;
}

std::optional<SeeMessage> SeePush::Update(const std::vector<SeenObject> &inRange, float dt) {
  m_sinceLastPush += dt;
  if (m_sinceLastPush < kMinInterval)
    return std::nullopt;

  std::vector<const SeenObject *> fresh;
  for (const auto &object : inRange) {
    if (IsNotable(object.category, object.questTarget) && !m_seen.contains(object.formId))
      fresh.push_back(&object);
  }
  if (fresh.empty())
    return std::nullopt;

  std::sort(fresh.begin(), fresh.end(), [](const SeenObject *left, const SeenObject *right) {
    return Objects::MoreImportant({left->formId, left->category, left->distance, left->questTarget},
                                  {right->formId, right->category, right->distance, right->questTarget});
  });
  if (fresh.size() > kMaxListed)
    fresh.resize(kMaxListed);

  std::string quest;
  std::string containers;
  std::string people;
  bool anyQuest = false;
  auto append = [](std::string &line, const SeenObject &object) {
    if (!line.empty())
      line += ", ";
    line += object.name + " (" + Units::FormatDistance(object.distance) + ")";
  };
  for (const SeenObject *object : fresh) {
    m_seen.insert(object->formId);
    if (object->questTarget) {
      anyQuest = true;
      append(quest, *object);
    } else if (object->category == Objects::Category::Container) {
      append(containers, *object);
    } else {
      append(people, *object);
    }
  }

  SeeMessage message;
  message.silent = !anyQuest;
  message.text = "## You see";
  if (!quest.empty())
    message.text += "\n- Quest target: " + quest;
  if (!people.empty())
    message.text += "\n- People and creatures: " + people;
  if (!containers.empty())
    message.text += "\n- Containers: " + containers;
  message.text += "\nUse query_nearby for ids and details.";
  m_sinceLastPush = 0.0f;
  return message;
}

} // namespace Context
