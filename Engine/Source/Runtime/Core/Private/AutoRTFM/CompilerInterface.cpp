// Copyright Epic Games, Inc. All Rights Reserved.

#if (defined(__AUTORTFM) && __AUTORTFM)

#include "ContextInlines.h"
#include "Debug.h"
#include "FunctionMapInlines.h"
#include "Memcpy.h"

namespace AutoRTFM
{

void AbortDueToBadAlignment(FContext* Context, void* Ptr, size_t Alignment, const char* Message = nullptr)
{
    Context->DumpState();
    fprintf(stderr, "Aborting because alignment error: expected alignment %zu, got pointer %p.\n", Alignment, Ptr);
    if (Message)
    {
        fprintf(stderr, "%s\n", Message);
    }
    abort();
}

void CheckAlignment(FContext* Context, void* Ptr, size_t AlignmentMask)
{
    if (reinterpret_cast<uintptr_t>(Ptr) & AlignmentMask)
    {
        AbortDueToBadAlignment(Context, Ptr, AlignmentMask + 1);
    }
}

extern "C" void autortfm_record_write(FContext* Context, void* Ptr, size_t Size)
{
	// check for writes to null here so we end up crashing in the user
	// code rather than in the autortfm runtime
	if (Ptr != nullptr)
	{
		Context->RecordWrite(Ptr, Size, true);
	}
}

extern "C" void* autortfm_lookup_function(FContext* Context, void* OriginalFunction, const char* Where)
{
    FDebug Debug(Context, OriginalFunction, nullptr, 0, 0, __FUNCTION__);
    return FunctionMapLookup(OriginalFunction, Context, Where);
}

extern "C" void autortfm_memcpy(void* Dst, const void* Src, size_t Size, FContext* Context)
{
    FDebug Debug(Context, Dst, Src, Size, 0, __FUNCTION__);
    Memcpy(Dst, Src, Size, Context);
}

extern "C" void autortfm_memmove(void* Dst, const void* Src, size_t Size, FContext* Context)
{
    FDebug Debug(Context, Dst, Src, Size, 0, __FUNCTION__);
    Memmove(Dst, Src, Size, Context);
}

extern "C" void autortfm_memset(void* Dst, int Value, size_t Size, FContext* Context)
{
    FDebug Debug(Context, Dst, nullptr, Size, 0, __FUNCTION__);
    Memset(Dst, Value, Size, Context);
}

extern "C" void autortfm_llvm_fail(FContext* Context, const char* Message)
{
    if (Message)
    {
        fprintf(stderr, "Transaction failing because of language issue:\n%s\n", Message);
    }
    else
    {
        fprintf(stderr, "Transaction failing because of language issue.\n");
    }
    Context->AbortByLanguageAndThrow();
}

extern "C" void autortfm_llvm_alignment_error(FContext* Context, void* Ptr, size_t Alignment, const char* Message)
{
    AbortDueToBadAlignment(Context, Ptr, Alignment, Message);
}

extern "C" void autortfm_llvm_error(FContext* Context, const char* Message)
{
    if (Message)
    {
        fprintf(stderr, "Aborting because LLVM error:\n%s\n", Message);
    }
    else
    {
        fprintf(stderr, "Aborting because LLVM error.\n");
    }
    abort();
}

} // namespace AutoRTFM

#endif // defined(__AUTORTFM) && __AUTORTFM
