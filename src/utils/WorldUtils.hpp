#pragma once

#include <cstdint>

class NiPoint3;
class TESObjectREFR;

namespace WorldUtils {

// The reference for a numeric form id, or null when it is missing, not a reference, deleted or disabled.
TESObjectREFR *LookupReference(uint32_t formId);
float Distance3D(const NiPoint3 &left, const NiPoint3 &right);

} // namespace WorldUtils
