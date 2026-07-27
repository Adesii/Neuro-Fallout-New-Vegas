#pragma once

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <type_traits>

#ifndef __vectorcall
#define __vectorcall
#endif

#ifndef _cdecl
#define _cdecl __cdecl
#endif

template <class Left, class Right>
constexpr std::common_type_t<Left, Right> max(Left left, Right right) {
    return left < right ? right : left;
}

#include "nvse/prefix.h"
