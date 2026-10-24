#include "VatsQueueShotAction.hpp"
#include "Actions/ActionData.hpp"
#include "Actions/Json/JsonSchema.hpp"
#include "Menus/VatsHandler.hpp"
#include <optional>
#include <utility>

namespace Actions::Combat {
namespace {

constexpr const char *kTarget = "target";
constexpr const char *kBodyPart = "body_part";

Definition BuildDefinition() {
  auto schema = Json::JsonSchema::Object();
  auto target = Json::JsonSchema::Integer();
  target.Minimum(1);
  auto bodyPart = Json::JsonSchema::Integer();
  bodyPart.Minimum(1);
  schema.Property(kTarget, std::move(target)).Property(kBodyPart, std::move(bodyPart));
  return {.name = "vats_queue_shot",
          .description = "Queue one V.A.T.S. shot at a body part. target and body_part are the numbers listed in the "
                         "VATS state. Each shot costs action points; queue as many as you can afford.",
          .schema = std::move(schema)};
}

const Definition kDefinition = BuildDefinition();

} // namespace

const Definition &VatsQueueShotAction::GetDefinition() const { return kDefinition; }

PreparedAction VatsQueueShotAction::Validate(const Request &request) {
  std::string error;
  auto data = ActionData::Parse(request.data, error);
  if (!data)
    return PreparedAction::Failure(std::move(error));
  if (!data->IsObject() || data->Size() != 2)
    return PreparedAction::Failure("Expected exactly target and body_part.");
  int target = 0;
  int bodyPart = 0;
  if (!data->GetInteger(kTarget, target) || !data->GetInteger(kBodyPart, bodyPart))
    return PreparedAction::Failure("target and body_part must be integers.");

  Menus::VatsHandler::ShotRequest shot;
  if (!Menus::VatsHandler::ValidateShot(target, bodyPart, shot, error))
    return PreparedAction::Failure(std::move(error));
  return PreparedAction::Success([shot]() { Menus::VatsHandler::StartShot(shot); },
                                 Revalidator([shot](std::string &revalidationError) {
                                   return Menus::VatsHandler::RevalidateShot(shot, revalidationError);
                                 }));
}

} // namespace Actions::Combat
