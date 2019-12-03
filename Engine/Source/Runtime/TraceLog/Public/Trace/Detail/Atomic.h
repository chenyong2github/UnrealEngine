// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

namespace Trace {
namespace Private {

////////////////////////////////////////////////////////////////////////////////
template <typename Type> Type	AtomicLoadRelaxed(Type volatile* Source);
template <typename Type> Type	AtomicLoadAcquire(Type volatile* Source);
template <typename Type> void	AtomicStoreRelaxed(Type volatile* Target, Type Value);
template <typename Type> void	AtomicStoreRelease(Type volatile* Target, Type Value);
template <typename Type> bool	AtomicCompareExchangeRelaxed(Type volatile* Target, Type New, Type Expected);
template <typename Type> bool	AtomicCompareExchangeAcquire(Type volatile* Target, Type New, Type Expected);
template <typename Type> bool	AtomicCompareExchangeRelease(Type volatile* Target, Type New, Type Expected);
uint32							AtomicIncrementRelaxed(uint32 volatile* Target);

} // namespace Private
} // namespace Trace

////////////////////////////////////////////////////////////////////////////////
#define IS_MSVC					0
#define IS_GCC_COMPATIBLE		0
#if defined(_MSC_VER) && !defined(__clang__)
#	undef  IS_MSVC
#	define IS_MSVC				1
#elif defined(__clang__) || defined(__GNUC__)
#	undef  IS_GCC_COMPATIBLE
#	define IS_GCC_COMPATIBLE	1
#endif

////////////////////////////////////////////////////////////////////////////////
#if IS_MSVC
#	include <intrin.h>
#	if defined(_M_ARM) || defined(_M_ARM64)
#		if defined(_M_ARM)
#			include <armintr.h>
#		else
#			include <arm64intr.h>
#		endif
#	endif
#endif

namespace Trace {
namespace Private {

////////////////////////////////////////////////////////////////////////////////
#if IS_MSVC
#	if defined(_M_ARM) || defined(_M_ARM64)
#		define INTERLOCKED_API(Name, Suffix, ...)	Name##_##Suffix(__VA_ARGS__)
#	elif defined(_M_IX86) || defined(_M_X64)
#		define INTERLOCKED_API(Name, Suffix, ...)	Name(__VA_ARGS__)
#	endif
#endif

////////////////////////////////////////////////////////////////////////////////
template <typename Type>
inline Type AtomicLoadRelaxed(Type volatile* Source)
{
	static_assert(sizeof(Type) == sizeof(void*), "");
#if IS_MSVC
#	if defined(_M_ARM)
		__int32 Value = __iso_volatile_load32((__int32 volatile*)Source);
		return (Type*)Value;
#	elif defined(_M_ARM64)
		__int64 Value = __iso_volatile_load64((__int64 volatile*)Source);
		return (Type*)Value;
#	else
		return *Source;
#	endif
#elif IS_GCC_COMPATIBLE
	return __atomic_load_n(Source, __ATOMIC_RELAXED);
#endif
}

////////////////////////////////////////////////////////////////////////////////
template <typename Type>
inline Type AtomicLoadAcquire(Type volatile* Source)
{
	static_assert(sizeof(Type) == sizeof(void*), "");
#if IS_MSVC
#	if defined(_M_ARM)
		__int32 Value = __iso_volatile_load32((__int32 volatile*)Source);
		__dmb(_ARM_BARRIER_ISH);
		return Type(Value);
#	elif defined(_M_ARM64)
		__int64 Value = __iso_volatile_load64((__int64 volatile*)Source);
		__dmb(_ARM64_BARRIER_ISH);
		return Type(Value);
#	else
		Type Value = *Source;
		_ReadWriteBarrier();
		return Value;
#	endif
#elif IS_GCC_COMPATIBLE
	return __atomic_load_n(Source, __ATOMIC_ACQUIRE);
#endif
}

////////////////////////////////////////////////////////////////////////////////
template <typename Type>
inline void AtomicStoreRelaxed(Type volatile* Target, Type Value)
{
	static_assert(sizeof(Type) == sizeof(void*), "");
#if IS_MSVC
#	if defined(_M_ARM)
		__iso_volatile_store32((__int32 volatile*)Target, UPTRINT(Value));
#	elif defined(_M_ARM64)
		__iso_volatile_store64((__int64 volatile*)Target, UPTRINT(Value));
#	else
		*Target = Value;
#	endif
#elif IS_GCC_COMPATIBLE
	return __atomic_store_n(Target, Value, __ATOMIC_RELAXED);
#endif
}

////////////////////////////////////////////////////////////////////////////////
template <typename Type>
inline void AtomicStoreRelease(Type volatile* Target, Type Value)
{
	static_assert(sizeof(Type) == sizeof(void*), "");
#if IS_MSVC
#	if defined(_M_ARM)
		__dmb(_ARM_BARRIER_ISH);
		__iso_volatile_store32((__int32 volatile*)Target, __int32(Value));
#	elif defined(_M_ARM64)
		__dmb(_ARM64_BARRIER_ISH);
		__iso_volatile_store64((__int64 volatile*)Target, __int64(Value));
#	else
		_ReadWriteBarrier();
		*Target = Value;
#	endif
#elif IS_GCC_COMPATIBLE
	return __atomic_store_n(Target, Value, __ATOMIC_RELEASE);
#endif
}

////////////////////////////////////////////////////////////////////////////////
template <typename Type>
inline bool AtomicCompareExchangeRelaxed(Type volatile* Target, Type New, Type Expected)
{
	static_assert(sizeof(Type) == sizeof(void*), "");
#if IS_MSVC
	return INTERLOCKED_API(_InterlockedCompareExchangePointer, _nf, (void* volatile*)Target, (void*)New, (void*)Expected) == (void*)Expected;
#elif IS_GCC_COMPATIBLE
	Type InOut = Expected;
	return __atomic_compare_exchange_n(Target, &InOut, New, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
#endif
}

////////////////////////////////////////////////////////////////////////////////
template <typename Type>
inline bool AtomicCompareExchangeAcquire(Type volatile* Target, Type New, Type Expected)
{
	static_assert(sizeof(Type) == sizeof(void*), "");
#if IS_MSVC
	return INTERLOCKED_API(_InterlockedCompareExchangePointer, _acq, (void* volatile*)Target, (void*)New, (void*)Expected) == (void*)Expected;
#elif IS_GCC_COMPATIBLE
	Type InOut = Expected;
	return __atomic_compare_exchange_n(Target, &InOut, New, true, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
#endif
}

////////////////////////////////////////////////////////////////////////////////
template <typename Type>
inline bool AtomicCompareExchangeRelease(Type volatile* Target, Type New, Type Expected)
{
	static_assert(sizeof(Type) == sizeof(void*), "");
#if IS_MSVC
	return INTERLOCKED_API(_InterlockedCompareExchangePointer, _rel, (void* volatile*)Target, (void*)New, (void*)Expected) == (void*)Expected;
#elif IS_GCC_COMPATIBLE
	Type InOut = Expected;
	return __atomic_compare_exchange_n(Target, &InOut, New, true, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
#endif
}

////////////////////////////////////////////////////////////////////////////////
inline uint32 AtomicIncrementRelaxed(uint32 volatile* Target)
{
	// Here we decrement the return of the MSVC path instead of incrementing the
	// GCC path as GCC better matches what x64's 'lock add' does.
#if IS_MSVC
	return INTERLOCKED_API(_InterlockedIncrement, _nf, (long volatile*)Target) - 1;
#elif IS_GCC_COMPATIBLE
	return __atomic_fetch_add(Target, 1, __ATOMIC_RELAXED);
#endif
}

#undef INTERLOCKED_API
#undef IS_GCC_COMPATIBLE
#undef IS_MSVC

} // namespace Private
} // namespace Trace
