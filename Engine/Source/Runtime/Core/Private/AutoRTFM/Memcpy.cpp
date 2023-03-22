// Copyright Epic Games, Inc. All Rights Reserved.

#if (defined(__AUTORTFM) && __AUTORTFM)
#include "Memcpy.h"
#include "ContextInlines.h"
#include "Debug.h"

namespace AutoRTFM
{

constexpr bool bVerbose = FDebug::bVerbose;

void* MemcpyToNew(void* InDst, const void* InSrc, size_t Size, FContext* Context)
{
    if (bVerbose)
    {
        fprintf(GetLogFile(), "MemcpyToNew(%p, %p, %zu)\n", InDst, InSrc, Size);
    }
    FDebug Debug(Context, InDst, InSrc, Size, 0, __FUNCTION__);
    AutoRTFM::Unreachable();
}

void* Memcpy(void* InDst, const void* InSrc, size_t Size, FContext* Context)
{
    if (bVerbose)
    {
        fprintf(GetLogFile(), "Memcpy(%p, %p, %zu)\n", InDst, InSrc, Size);
    }
    FDebug Debug(Context, InDst, InSrc, Size, 0, __FUNCTION__);
    
    Context->RecordWrite(InDst, Size, true);

    return memcpy(InDst, InSrc, Size);
}

void* Memmove(void* InDst, const void* InSrc, size_t Size, FContext* Context)
{
    if (bVerbose)
    {
        fprintf(GetLogFile(), "Memmove(%p, %p, %zu)\n", InDst, InSrc, Size);
    }
    FDebug Debug(Context, InDst, InSrc, Size, 0, __FUNCTION__);

	Context->RecordWrite(InDst, Size, true);

	return memmove(InDst, InSrc, Size);
}

void* Memset(void* InDst, int Value, size_t Size, FContext* Context)
{
    if (bVerbose)
    {
        fprintf(GetLogFile(), "Memset(%p, %d, %zu)\n", InDst, Value, Size);
    }
    FDebug Debug(Context, InDst, nullptr, Size, 0, __FUNCTION__);

	Context->RecordWrite(InDst, Size, true);

	return memset(InDst, Value, Size);
}

} // namespace AutoRTFM

#endif // defined(__AUTORTFM) && __AUTORTFM
