// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "Containers/ContainerAllocationPolicies.h"

// If CHAOS_CHECK_UNCHECKED_ARRAY is 1, we still enable range checks on "unchecked" arrays.
// This is for debug builds or sanity checks in other builds.
#ifndef CHAOS_CHECK_UNCHECKED_ARRAY
#if UE_BUILD_DEBUG
#define CHAOS_CHECK_UNCHECKED_ARRAY 1
#else
#define CHAOS_CHECK_UNCHECKED_ARRAY 0
#endif
#endif

namespace Chaos
{
	/**
	 * @brief A fixed allocator without array bounds checking except in Debug builds.
	 *
	 * In non-debug builds this offers no saftey at all - it is effectively a C-style array.
	 *
	 * This is for use in critical path code where bounds checking would be costly and we want
	 * to ship a build with most asserts enabled (e.g. the server)
	*/
	template<int32 NumInlineElements>
	class TUncheckedFixedAllocator : public TFixedAllocator<NumInlineElements>
	{
	public:
#if !CHAOS_CHECK_UNCHECKED_ARRAY
		enum { RequireRangeCheck = false };
#endif
	};

	class FUncheckedHeapAllocator : public FHeapAllocator
	{
	public:
#if !CHAOS_CHECK_UNCHECKED_ARRAY
		enum { RequireRangeCheck = false };
#endif
	};

	template<typename T, int32 N>
	using TUncheckedFixedArray = TArray<T, TUncheckedFixedAllocator<N>>;

	template<typename T>
	using TUncheckedArray = TArray<T, FUncheckedHeapAllocator>;
}

template<int32 NumInlineElements>
struct TAllocatorTraits<Chaos::TUncheckedFixedAllocator<NumInlineElements>> : TAllocatorTraits<TFixedAllocator<NumInlineElements>>
{
};

template<>
struct TAllocatorTraits<Chaos::FUncheckedHeapAllocator> : TAllocatorTraits<FHeapAllocator>
{
};


