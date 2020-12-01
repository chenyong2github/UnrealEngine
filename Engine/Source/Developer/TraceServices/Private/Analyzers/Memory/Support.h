// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/UnrealMemory.h"

#if defined(_MSC_VER)
#	include <intrin.h>
#endif

namespace Trace {
namespace TraceServices {

////////////////////////////////////////////////////////////////////////////////
inline uint32 UnsafeCountLeadingZeros(uint32 Value)
{
	/* it is assumed that the caller knows not to counting leading zeros of 0 */
#ifdef _MSC_VER
	return __lzcnt(Value);
#else
	return __builtin_clz(Value);
#endif
}

#define PROF_SCOPE(...)

////////////////////////////////////////////////////////////////////////////////
class FTrackerBuffer
{
public:
	template <class T> static T*	AllocTemp();
	template <class T> static T*	CallocTemp(uint32 Max);
	static void						FreeTemp(void*);

	template <class T> static T*	Alloc();
	template <class T> static T*	AllocRaw(SIZE_T Size, uint32 Alignment=alignof(T));
	template <class T> static T*	Calloc(uint32 Max);
	template <class T> static T*	Realloc(T* Prev, uint32 Max);
	static void						Free(void*);

private:
};

////////////////////////////////////////////////////////////////////////////////
template <class T>
inline T* FTrackerBuffer::AllocTemp()
{
	return Alloc<T>();
}

////////////////////////////////////////////////////////////////////////////////
template <class T>
inline T* FTrackerBuffer::CallocTemp(uint32 Num)
{
	return Calloc<T>(Num);
}

////////////////////////////////////////////////////////////////////////////////
inline void FTrackerBuffer::FreeTemp(void* Ptr)
{
	Free(Ptr);
}

////////////////////////////////////////////////////////////////////////////////
template <class T>
inline T* FTrackerBuffer::Alloc()
{
	void* Out = FMemory::Malloc(sizeof(T), alignof(T));
	return new (Out) T();
}

////////////////////////////////////////////////////////////////////////////////
template <class T>
inline T* FTrackerBuffer::AllocRaw(SIZE_T Size, uint32 Alignment)
{
	void* Out = FMemory::Malloc(Size, Alignment);
	return (T*)Out;
}

////////////////////////////////////////////////////////////////////////////////
template <class T>
inline T* FTrackerBuffer::Calloc(uint32 Num)
{
	SIZE_T Bytes = sizeof(T) + (sizeof(T::Items[0]) * Num);
	void* Ptr = FMemory::Malloc(Bytes, alignof(T));
	T* Out = new (Ptr) T();
	Out->Max = Num;
	return Out;
}

////////////////////////////////////////////////////////////////////////////////
template <class T>
inline T* FTrackerBuffer::Realloc(T* Prev, uint32 Num)
{
	SIZE_T Bytes = sizeof(T) + (sizeof(T::Items[0]) * Num);
	auto* Out = (T*)FMemory::Realloc(Prev, Bytes, alignof(T));
	Out->Max = Num;
	return Out;
}

////////////////////////////////////////////////////////////////////////////////
inline void FTrackerBuffer::Free(void* Ptr)
{
	FMemory::Free(Ptr);
}

} // namespace TraceServices
} // namespace Trace

/* vim: set noet : */
