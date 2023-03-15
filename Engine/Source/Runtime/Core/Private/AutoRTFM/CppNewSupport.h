// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AutoRTFM/AutoRTFM.h"

namespace AutoRTFM
{

#ifndef _WIN32
extern "C" void* _Znwm(size_t Size); // C++ new
extern "C" void* _Znam(size_t Size); // C++ array new
extern "C" void _ZdlPv(void* Ptr); // C++ delete

inline void* CppNew(size_t Size)
{
    return _Znwm(Size);
}
inline void CppDelete(void* Ptr)
{
    _ZdlPv(Ptr);
}
inline void CppDeleteWithSize(void* Ptr, size_t Size)
{
    CppDelete(Ptr);
}
#endif

}

