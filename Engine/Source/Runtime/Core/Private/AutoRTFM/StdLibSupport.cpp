// Copyright Epic Games, Inc. All Rights Reserved.

#if (defined(__AUTORTFM) && __AUTORTFM)
#include "ContextInlines.h"
#include "Utils.h"
#include "Memcpy.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <float.h>
#include <math.h>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace AutoRTFM
{

UE_AUTORTFM_REGISTER_OPEN_FUNCTION_EXPLICIT(memcpy, Memcpy);
UE_AUTORTFM_REGISTER_OPEN_FUNCTION_EXPLICIT(memmove, Memmove);
UE_AUTORTFM_REGISTER_OPEN_FUNCTION_EXPLICIT(memset, Memset);

// TODO: We need this to stop link errors looking for `DidAllocate` below?
#pragma clang optimize off

void* STM_malloc(size_t Size, FContext* Context)
{
    void* Result = malloc(Size);
    Context->GetCurrentTransaction()->DeferUntilAbort([Result]
    {
        free(Result);
    });
    Context->DidAllocate(Result, Size);
    return Result;
}

#pragma clang optimize on

UE_AUTORTFM_REGISTER_OPEN_FUNCTION(malloc);

void STM_free(void* Ptr, FContext* Context)
{
    if (Ptr)
    {
		const size_t AllocSize = GetAllocationSize(Ptr);
        Context->WillDeallocate(Ptr, AllocSize);

        Context->GetCurrentTransaction()->DeferUntilCommit([Ptr]
        {
            free(Ptr);
        });
    }
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(free);

void* STM_realloc(void* Ptr, size_t Size, FContext* Context)
{
    void* NewObject = STM_malloc(Size, Context);
    if (Ptr)
    {
    	const size_t OldSize = GetAllocationSize(Ptr);
        MemcpyToNew(NewObject, Ptr, std::min(OldSize, Size), Context);
        STM_free(Ptr, Context);
    }
    return NewObject;
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(realloc);


size_t STM_strlen(const char* Str, FContext* Context)
{
	AutoRTFM::Unreachable();
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(strlen);

// FIXME: This is only correct when:
// - Str is newly allocated
// - Format is either newly allocated or not mutated
// - any strings passed as arguments are either newly allocated or not mutated
int STM_snprintf(char* Str, size_t Size, char* Format, FContext* Context, ...)
{
    va_list ArgList;
    va_start(ArgList, Context);
    int Result = vsnprintf(Str, Size, Format, ArgList);
    va_end(ArgList);
    return Result;
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(snprintf);

char *STM_strncpy(char* Dst, const char* Src, size_t Len, FContext* Context)
{
    AutoRTFM::Unreachable();
}

#ifdef _MSC_VER
/*
   Disable warning about deprecated STD C functions.
*/
#pragma warning(disable : 4996)

#pragma warning(push)
#endif

UE_AUTORTFM_REGISTER_OPEN_FUNCTION(strncpy);

#ifdef _MSC_VER
#pragma warning(pop)
#endif

int STM_printf(const char* Format, FContext* Context, ...)
{
    va_list ArgList;
    va_start(ArgList, Context);
    int Result = vprintf(Format, ArgList);
    va_end(ArgList);
    return Result;
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(printf);

int STM_putchar(int Char, FContext* Context)
{
    return putchar(Char);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(putchar);

int STM_puts(const char* Str, FContext* Context)
{
    return puts(Str);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(puts);

#ifdef _WIN32

FILE* STM___acrt_iob_func(int Index, FContext* Context)
{
    switch (Index)
    {
    case 1:
    case 2:
        return __acrt_iob_func(Index);
    default:
        fprintf(stderr, "Attempt to get file descriptor %d (not 1 or 2) in __acrt_iob_func.\n", Index);
        Context->AbortByLanguageAndThrow();
        return NULL;
    }
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(__acrt_iob_func);

UE_AUTORTFM_REGISTER_SELF_FUNCTION(__stdio_common_vfprintf);

UE_AUTORTFM_REGISTER_SELF_FUNCTION(puts);

UE_AUTORTFM_REGISTER_SELF_FUNCTION(__stdio_common_vswprintf);

UE_AUTORTFM_REGISTER_SELF_FUNCTION(__stdio_common_vfwprintf);

#else // _WIN32 -> so !_WIN32
extern "C" size_t _ZNSt3__112__next_primeEm(size_t N) __attribute__((weak));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(_ZNSt3__112__next_primeEm);
#endif // _WIN32 -> so end of !_WIN32

UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<float(*)(float, float)>(&powf));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<double(*)(double, double)>(&pow));

#ifdef _WIN32
UE_AUTORTFM_REGISTER_SELF_FUNCTION(_isnan);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(_finite);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(IsDebuggerPresent);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(EnterCriticalSection);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(LeaveCriticalSection);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(QueryPerformanceCounter);
#endif

wchar_t* STM_wcsncpy(wchar_t* Dst, const wchar_t* Src, size_t Count, FContext* Context)
{
	AutoRTFM::Unreachable();
}

#ifdef _MSC_VER
/*
   Disable warning about deprecated STD C functions.
*/
#pragma warning(disable : 4996)

#pragma warning(push)
#endif

UE_AUTORTFM_REGISTER_OPEN_FUNCTION(wcsncpy);

#ifdef _MSC_VER
#pragma warning(pop)
#endif

int STM_atexit(void(__cdecl*Callback)(void), FContext* Context)
{
	Context->GetCurrentTransaction()->DeferUntilCommit([Callback]
		{
			atexit(Callback);
		});

	return 0;
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(atexit);

} // namespace AutoRTFM

#endif // defined(__AUTORTFM) && __AUTORTFM
