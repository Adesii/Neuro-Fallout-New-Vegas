#pragma once

#include "Utils/DebugLog.hpp"
#include "common.hpp"

#include <bit>
#include <cstdint>
#include <string>
#include <unordered_map>

// Yes these templates were created by AI. I'm not that crazy to write this manually
// Gemini free to be exact
#define EXPAND(x) x

// ==========================================
// 1. DECLARATIONS (Types + Names + Newlines)
// ==========================================
#define DECLARE_0() ""
#define DECLARE_2(t1, n1) #t1 " " #n1 "\n"
#define DECLARE_4(t1, n1, t2, n2) DECLARE_2(t1, n1) DECLARE_2(t2, n2)
#define DECLARE_6(t1, n1, t2, n2, t3, n3) DECLARE_4(t1, n1, t2, n2) DECLARE_2(t3, n3)
#define DECLARE_8(t1, n1, t2, n2, t3, n3, t4, n4) DECLARE_6(t1, n1, t2, n2, t3, n3) DECLARE_2(t4, n4)
#define DECLARE_10(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5) DECLARE_8(t1, n1, t2, n2, t3, n3, t4, n4) DECLARE_2(t5, n5)
#define DECLARE_12(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6)                                                     \
  DECLARE_10(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5) DECLARE_2(t6, n6)
#define DECLARE_14(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7)                                             \
  DECLARE_12(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6) DECLARE_2(t7, n7)
#define DECLARE_16(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8)                                     \
  DECLARE_14(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7) DECLARE_2(t8, n8)
#define DECLARE_18(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9)                             \
  DECLARE_16(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8) DECLARE_2(t9, n9)
#define DECLARE_20(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10)                   \
  DECLARE_18(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9) DECLARE_2(t10, n10)
#define DECLARE_22(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11)         \
  DECLARE_20(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10) DECLARE_2(t11, n11)
#define DECLARE_24(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11, t12,    \
                   n12)                                                                                                \
  DECLARE_22(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11)               \
  DECLARE_2(t12, n12)
#define DECLARE_26(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11, t12,    \
                   n12, t13, n13)                                                                                      \
  DECLARE_24(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11, t12, n12)     \
  DECLARE_2(t13, n13)
#define DECLARE_28(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11, t12,    \
                   n12, t13, n13, t14, n14)                                                                            \
  DECLARE_26(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11, t12, n12,     \
             t13, n13)                                                                                                 \
  DECLARE_2(t14, n14)
#define DECLARE_30(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11, t12,    \
                   n12, t13, n13, t14, n14, t15, n15)                                                                  \
  DECLARE_28(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11, t12, n12,     \
             t13, n13, t14, n14)                                                                                       \
  DECLARE_2(t15, n15)

// ==========================================
// 2. PARAMETERS (Names + Commas)
// ==========================================
#define PARAM_0() ""
#define PARAM_2(t1, n1) #n1
#define PARAM_4(t1, n1, t2, n2) PARAM_2(t1, n1) ", " #n2
#define PARAM_6(t1, n1, t2, n2, t3, n3) PARAM_4(t1, n1, t2, n2) ", " #n3
#define PARAM_8(t1, n1, t2, n2, t3, n3, t4, n4) PARAM_6(t1, n1, t2, n2, t3, n3) ", " #n4
#define PARAM_10(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5) PARAM_8(t1, n1, t2, n2, t3, n3, t4, n4) ", " #n5
#define PARAM_12(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6)                                                       \
  PARAM_10(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5) ", " #n6
#define PARAM_14(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7)                                               \
  PARAM_12(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6) ", " #n7
#define PARAM_16(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8)                                       \
  PARAM_14(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7) ", " #n8
#define PARAM_18(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9)                               \
  PARAM_16(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8) ", " #n9
#define PARAM_20(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10)                     \
  PARAM_18(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9) ", " #n10
#define PARAM_22(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11)           \
  PARAM_20(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10) ", " #n11
#define PARAM_24(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11, t12, n12) \
  PARAM_22(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11) ", " #n12
#define PARAM_26(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11, t12, n12, \
                 t13, n13)                                                                                             \
  PARAM_24(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11, t12, n12)       \
  ", " #n13
#define PARAM_28(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11, t12, n12, \
                 t13, n13, t14, n14)                                                                                   \
  PARAM_26(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11, t12, n12, t13,  \
           n13)                                                                                                        \
  ", " #n14
#define PARAM_30(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11, t12, n12, \
                 t13, n13, t14, n14, t15, n15)                                                                         \
  PARAM_28(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11, t12, n12, t13,  \
           n13, t14, n14)                                                                                              \
  ", " #n15

// ==========================================
// 3. VALUES (Names + Spaces)
// ==========================================
#define VALUE_0() ""
#define VALUE_2(t1, n1) #n1
#define VALUE_4(t1, n1, t2, n2) VALUE_2(t1, n1) " " #n2
#define VALUE_6(t1, n1, t2, n2, t3, n3) VALUE_4(t1, n1, t2, n2) " " #n3
#define VALUE_8(t1, n1, t2, n2, t3, n3, t4, n4) VALUE_6(t1, n1, t2, n2, t3, n3) " " #n4
#define VALUE_10(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5) VALUE_8(t1, n1, t2, n2, t3, n3, t4, n4) " " #n5
#define VALUE_12(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6)                                                       \
  VALUE_10(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5) " " #n6
#define VALUE_14(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7)                                               \
  VALUE_12(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6) " " #n7
#define VALUE_16(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8)                                       \
  VALUE_14(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7) " " #n8
#define VALUE_18(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9)                               \
  VALUE_16(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8) " " #n9
#define VALUE_20(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10)                     \
  VALUE_18(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9) " " #n10
#define VALUE_22(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11)           \
  VALUE_20(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10) " " #n11
#define VALUE_24(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11, t12, n12) \
  VALUE_22(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11) " " #n12
#define VALUE_26(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11, t12, n12, \
                 t13, n13)                                                                                             \
  VALUE_24(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11, t12, n12)       \
  " " #n13
#define VALUE_28(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11, t12, n12, \
                 t13, n13, t14, n14)                                                                                   \
  VALUE_26(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11, t12, n12, t13,  \
           n13)                                                                                                        \
  " " #n14
#define VALUE_30(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11, t12, n12, \
                 t13, n13, t14, n14, t15, n15)                                                                         \
  VALUE_28(t1, n1, t2, n2, t3, n3, t4, n4, t5, n5, t6, n6, t7, n7, t8, n8, t9, n9, t10, n10, t11, n11, t12, n12, t13,  \
           n13, t14, n14)                                                                                              \
  " " #n15
// ==========================================
// 4. THE CORRECTED ROUTER (Handles 1 to 30 tokens)
// ==========================================
#define COR_COUNT(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, \
                  _23, _24, _25, _26, _27, _28, _29, _30, N, ...)                                                      \
  N

#define DYNAMIC_DECLARE(...)                                                                                           \
  EXPAND(COR_COUNT(__VA_ARGS__, DECLARE_30, DECLARE_29, DECLARE_28, DECLARE_27, DECLARE_26, DECLARE_25, DECLARE_24,    \
                   DECLARE_23, DECLARE_22, DECLARE_21, DECLARE_20, DECLARE_19, DECLARE_18, DECLARE_17, DECLARE_16,     \
                   DECLARE_15, DECLARE_14, DECLARE_13, DECLARE_12, DECLARE_11, DECLARE_10, DECLARE_9, DECLARE_8,       \
                   DECLARE_7, DECLARE_6, DECLARE_5, DECLARE_4, DECLARE_3, DECLARE_2, DECLARE_1))(__VA_ARGS__)

#define DYNAMIC_PARAM(...)                                                                                             \
  EXPAND(COR_COUNT(__VA_ARGS__, PARAM_30, PARAM_29, PARAM_28, PARAM_27, PARAM_26, PARAM_25, PARAM_24, PARAM_23,        \
                   PARAM_22, PARAM_21, PARAM_20, PARAM_19, PARAM_18, PARAM_17, PARAM_16, PARAM_15, PARAM_14, PARAM_13, \
                   PARAM_12, PARAM_11, PARAM_10, PARAM_9, PARAM_8, PARAM_7, PARAM_6, PARAM_5, PARAM_4, PARAM_3,        \
                   PARAM_2, PARAM_1))(__VA_ARGS__)

#define DYNAMIC_VALUE(...)                                                                                             \
  EXPAND(COR_COUNT(__VA_ARGS__, VALUE_30, VALUE_29, VALUE_28, VALUE_27, VALUE_26, VALUE_25, VALUE_24, VALUE_23,        \
                   VALUE_22, VALUE_21, VALUE_20, VALUE_19, VALUE_18, VALUE_17, VALUE_16, VALUE_15, VALUE_14, VALUE_13, \
                   VALUE_12, VALUE_11, VALUE_10, VALUE_9, VALUE_8, VALUE_7, VALUE_6, VALUE_5, VALUE_4, VALUE_3,        \
                   VALUE_2, VALUE_1))(__VA_ARGS__)

// ==========================================
// 5. FIXED MASTER TEMPLATE
// ==========================================
// Base Variadic Macro for 1 to 15 parameters
#define CREATE_PLUGINSCRIPT(name, ...)                                                                                 \
  constexpr char k##name##Script[] = "" DYNAMIC_DECLARE(__VA_ARGS__) "begin function { " DYNAMIC_PARAM(                \
      __VA_ARGS__) " }\n"                                                                                              \
                   "SetFunctionValue (" #name " " DYNAMIC_VALUE(__VA_ARGS__) ")\n"                                     \
                                                                             "end\n"

// Explicit Overload for 0 parameters to bypass the router completely
#define CREATE_PLUGINSCRIPT_0(name)                                                                                    \
  constexpr char k##name##Script[] = "begin function {  }\n"                                                           \
                                     "SetFunctionValue (" #name " )\n"                                                 \
                                     "end\n"

namespace CachedScripts {

inline std::unordered_map<std::string, Script *> scripts;

inline Script *Get(const char *source) {
  auto [it, inserted] = scripts.try_emplace(source);
  if (inserted) {
    if (!g_script) {
      _ERROR("Cannot compile plugin script: xNVSE's script interface is unavailable.");
      return nullptr;
    }

    it->second = g_script->CompileScript(source);
    if (!it->second) {
      _ERROR("Failed to compile plugin script:\n%s", source);
    }
  }
  return it->second;
}

inline void *FloatArg(float value) {
  return reinterpret_cast<void *>(static_cast<std::uintptr_t>(std::bit_cast<std::uint32_t>(value)));
}

template <typename... Args>
bool Call(const char *source, TESObjectREFR *callingRef, NVSEArrayVarInterface::Element &result, Args... args) {
  Script *script = Get(source);
  if (!script) {
    return false;
  }

  const bool success = g_script->CallFunction(script, callingRef, nullptr, &result, sizeof...(args), args...);
  if (!success) {
    _ERROR("Failed to execute cached plugin script:\n%s", source);
  }
  return success;
}
template <typename... Args> bool CallNoResult(const char *source, Args... args) {
  Script *script = Get(source);
  if (!script) {
    return false;
  }

  const bool success = g_script->CallFunction(script, nullptr, nullptr, nullptr, sizeof...(args), args...);
  if (!success) {
    _ERROR("Failed to execute cached plugin script:\n%s", source);
  }
  return success;
}

template <typename... Args>
bool CallC(const char *source, TESObjectREFR *callingRef, TESObjectREFR *containerRef,
           NVSEArrayVarInterface::Element &result, Args... args) {
  Script *script = Get(source);
  if (!script) {
    return false;
  }

  const bool success = g_script->CallFunction(script, callingRef, containerRef, &result, sizeof...(args), args...);
  if (!success) {
    _ERROR("Failed to execute cached plugin script:\n%s", source);
  }
  return success;
}

} // namespace CachedScripts
