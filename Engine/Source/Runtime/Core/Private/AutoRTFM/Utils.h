// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AutoRTFM/AutoRTFM.h"
#define __STDC_FORMAT_MACROS 1
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#ifdef __APPLE__
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif

#define ASSERT(exp) do { \
    if (!(exp)) { \
        fprintf(stderr, "%s:%d:%s: assertion %s failed.\n", __FILE__, __LINE__, __PRETTY_FUNCTION__, #exp); \
        abort(); \
    } \
} while (false)

namespace AutoRTFM
{

[[noreturn]] inline void Unreachable()
{
    fprintf(stderr, "Unreachable encountered!\n");

#ifdef _WIN32
    __assume(false);
#else
    __builtin_unreachable();
#endif
}

FILE* GetLogFile();

std::string GetFunctionDescription(void* FunctionPtr);

template<typename TReturnType, typename... TParameterTypes>
std::string GetFunctionDescription(TReturnType (*FunctionPtr)(TParameterTypes...))
{
    return GetFunctionDescription(reinterpret_cast<void*>(FunctionPtr));
}

inline size_t GetAllocationSize(void* const Ptr)
{
#if defined(__APPLE__)
    return malloc_size(Ptr);
#elif defined(_WIN32)
    return _msize(Ptr);
#else
    return malloc_usable_size(Ptr);
#endif
}

template<size_t A, size_t B> struct PrettyStaticAssert final
{
  static_assert(A == B, "Not equal");
  static constexpr bool _cResult = (A == B);
};

} // namespace AutoRTFM
