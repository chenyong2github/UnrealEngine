// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Context.h"
#include "FunctionMap.h"

namespace AutoRTFM
{

inline void* FunctionMapLookup(void* OldFunction, FContext* Context, const char* Where)
{
    void* Result = FunctionMapTryLookup(OldFunction);
    if (!Result)
    {
        fprintf(stderr, "Could not find function %p (%s)\n", OldFunction, TCHAR_TO_ANSI(*GetFunctionDescription(OldFunction)));
        if (Where)
        {
            fprintf(stderr, "%s\n", Where);
        }
        Context->AbortByLanguageAndThrow();
    }
    return Result;
}

template<typename TReturnType, typename... TParameterTypes>
auto FunctionMapLookup(TReturnType (*Function)(TParameterTypes...), FContext* Context, const char* Where) -> TReturnType (*)(TParameterTypes..., FContext*)
{
    return reinterpret_cast<TReturnType (*)(TParameterTypes..., FContext*)>(FunctionMapLookup(reinterpret_cast<void*>(Function), Context, Where));
}

} // namespace AutoRTFM

