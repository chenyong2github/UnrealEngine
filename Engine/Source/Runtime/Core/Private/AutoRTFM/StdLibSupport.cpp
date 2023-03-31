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

#ifdef __APPLE__
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif

#if PLATFORM_WINDOWS
#define NOMINMAX
#include <windows.h>
#endif

namespace AutoRTFM
{

UE_AUTORTFM_REGISTER_OPEN_FUNCTION_EXPLICIT(memcpy, Memcpy);
UE_AUTORTFM_REGISTER_OPEN_FUNCTION_EXPLICIT(memmove, Memmove);
UE_AUTORTFM_REGISTER_OPEN_FUNCTION_EXPLICIT(memset, Memset);

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

UE_AUTORTFM_REGISTER_OPEN_FUNCTION(malloc);

void STM_free(void* Ptr, FContext* Context)
{
    if (Ptr)
    {
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
#if defined(__APPLE__)
		const size_t OldSize = malloc_size(Ptr);
#elif defined(_WIN32)
		const size_t OldSize = _msize(Ptr);
#else
		const size_t OldSize = malloc_usable_size(Ptr);
#endif
        MemcpyToNew(NewObject, Ptr,  FMath::Min(OldSize, Size), Context);
        STM_free(Ptr, Context);
    }
    return NewObject;
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(realloc);

char* STM_strcpy(char* const Dst, const char* const Src, FContext* const Context)
{
    const size_t SrcLen = strlen(Src);

    Context->RecordWrite(Dst, SrcLen, true);
    return strcpy(Dst, Src);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(strcpy);

char* STM_strncpy(char* const Dst, const char* const Src, const size_t Num, FContext* const Context)
{
    Context->RecordWrite(Dst, Num, true);
    return strncpy(Dst, Src, Num);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(strncpy);

char* STM_strcat(char* const Dst, const char* const Src, FContext* const Context)
{
    const size_t DstLen = strlen(Dst);
    const size_t SrcLen = strlen(Src);

    Context->RecordWrite(Dst + DstLen, SrcLen + 1, true);
    return strcat(Dst, Src);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(strcat);

char* STM_strncat(char* const Dst, const char* const Src, const size_t Num, FContext* const Context)
{
    const size_t DstLen = strlen(Dst);

    Context->RecordWrite(Dst + DstLen, Num + 1, true);
    return strncat(Dst, Src, Num);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(strncat);

UE_AUTORTFM_REGISTER_SELF_FUNCTION(memcmp);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(strcmp);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(strncmp);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<const char*(*)(const char*, int)>(&strchr));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<const char* (*)(const char*, int)>(&strrchr));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<const char* (*)(const char*, const char*)>(&strstr));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(strlen);

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

#if PLATFORM_WINDOWS

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
UE_AUTORTFM_REGISTER_SELF_FUNCTION(__stdio_common_vsprintf);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(__stdio_common_vswprintf);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(__stdio_common_vfwprintf);

#else // PLATFORM_WINDOWS -> so !PLATFORM_WINDOWS
extern "C" size_t _ZNSt3__112__next_primeEm(size_t N) __attribute__((weak));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(_ZNSt3__112__next_primeEm);
#endif // PLATFORM_WINDOWS -> so end of !PLATFORM_WINDOWS

UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<float(*)(float, float)>(&powf));
UE_AUTORTFM_REGISTER_SELF_FUNCTION(static_cast<double(*)(double, double)>(&pow));

#if PLATFORM_WINDOWS
UE_AUTORTFM_REGISTER_SELF_FUNCTION(_tcslen);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(_isnan);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(_finite);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(IsDebuggerPresent);

UE_AUTORTFM_REGISTER_SELF_FUNCTION(QueryPerformanceCounter);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(QueryPerformanceFrequency);
UE_AUTORTFM_REGISTER_SELF_FUNCTION(GetCurrentThreadId);

UE_AUTORTFM_REGISTER_SELF_FUNCTION(TlsGetValue);

BOOL STM_TlsSetValue(DWORD dwTlsIndex, LPVOID lpTlsValue, FContext* Context)
{
	LPVOID CurrentValue = TlsGetValue(dwTlsIndex);

	AutoRTFM::OpenAbort([dwTlsIndex, CurrentValue]
	{
		TlsSetValue(dwTlsIndex, CurrentValue);
	});

	return TlsSetValue(dwTlsIndex, lpTlsValue);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(TlsSetValue);
#endif // PLATFORM_WINDOWS

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
