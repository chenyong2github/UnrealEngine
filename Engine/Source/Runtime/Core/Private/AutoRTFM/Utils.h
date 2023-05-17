// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AutoRTFM/AutoRTFM.h"
#define __STDC_FORMAT_MACROS 1
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "Containers/UnrealString.h"
#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAutoRTFM, Display, All)

#define ASSERT(exp) UE_CLOG(!(exp), LogAutoRTFM, Fatal, TEXT("%s:%d:%s: assertion %s failed."), __FILE__, __LINE__, __PRETTY_FUNCTION__, #exp)

namespace AutoRTFM
{

[[noreturn]] inline void Unreachable()
{
	UE_LOG(LogAutoRTFM, Fatal, TEXT("Unreachable encountered!"));

#if PLATFORM_WINDOWS
    __assume(false);
#else
    __builtin_unreachable();
#endif // PLATFORM_WINDOWS
}

FString GetFunctionDescription(void* FunctionPtr);

template<typename TReturnType, typename... TParameterTypes>
FString GetFunctionDescription(TReturnType (*FunctionPtr)(TParameterTypes...))
{
    return GetFunctionDescription(reinterpret_cast<void*>(FunctionPtr));
}

template<size_t A, size_t B> struct PrettyStaticAssert final
{
  static_assert(A == B, "Not equal");
  static constexpr bool _cResult = (A == B);
};

} // namespace AutoRTFM
