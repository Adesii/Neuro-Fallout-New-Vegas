#pragma once

#include <cstdint>
#include <functional>

class NiPoint3;
class TESObjectCELL;
class TESObjectREFR;

namespace WorldUtils {

// The reference for a numeric form id, or null when it is missing, not a reference, deleted or disabled.
TESObjectREFR *LookupReference(uint32_t formId);
float Distance3D(const NiPoint3 &left, const NiPoint3 &right);

// Visits each enabled, undeleted reference in the loaded interior cell, or the loaded exterior grid, exactly once
// (the player included). The cell passed is the one that holds the reference.
void ForEachLoadedReference(const std::function<void(TESObjectREFR *, TESObjectCELL *)> &visit);

} // namespace WorldUtils
