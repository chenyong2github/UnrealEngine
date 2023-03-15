// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Context.h"
#include "Utils.h"

namespace AutoRTFM
{

class FDebug final
{
public:
	static constexpr bool bVerbose = false;
    static constexpr bool bCheck = false;
    static constexpr bool bCheckOnCommit = false;

    FDebug(FContext* Context, const void* Dst, const void* Src, size_t Size, size_t Align, const char* Action)
        : Dst(Dst), Src(Src), Size(Size), Align(Align), Action(Action), Context(Context)
    {
        if (bVerbose)
        {
            fprintf(GetLogFile(), "Compiler: %s [dst %p | src %p | size %zu | align %zu]\n", Action, Dst, Src, Size, Align);
        }
    }

    ~FDebug()
    {
    }

private:
    const void* const Dst;
    const void* const Src;
    const size_t Size;
    const size_t Align;
    const char* const Action;
    FContext* const Context;
};

} // namespace AutoRTFM

