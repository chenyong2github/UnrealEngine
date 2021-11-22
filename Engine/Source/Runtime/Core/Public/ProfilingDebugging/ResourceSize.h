// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Misc/AssertionMacros.h"

/** Indicate what types of resources should be included for calculating used memory */
namespace EResourceSizeMode
{
	enum Type
	{
		/** Only include memory used by non-UObject resources that are directly owned by this UObject. This is used to show memory actually used at runtime */
		Exclusive,
		/** Include exclusive resources and UObject serialized memory for this and all child UObjects, but not memory for external referenced assets or editor only members. This is used in the editor to estimate maximum required memory */
		EstimatedTotal,
	};
};

/**
 * Struct used to count up the amount of memory used by a resource.
 * This is typically used for assets via UObject::GetResourceSizeEx(...).
 */
struct FResourceSizeEx
{
public:

	/**
	 * Default constructor. 
	 */
	explicit FResourceSizeEx()
		: ResourceSizeMode(EResourceSizeMode::Exclusive)
	{
	}

	/**
	 * Construct using a given mode. 
	 */
	explicit FResourceSizeEx(const EResourceSizeMode::Type InResourceSizeMode)
		: ResourceSizeMode(InResourceSizeMode)
	{
	}

	/**
	 * Construct from known sizes. 
	 */
	FResourceSizeEx(const EResourceSizeMode::Type InResourceSizeMode, const SIZE_T InDedicatedSystemMemoryBytes, const SIZE_T InDedicatedVideoMemoryBytes)
		: ResourceSizeMode(InResourceSizeMode)
	{
		DedicatedSystemMemoryBytesMap.Add(TEXT("Untracked Memory"), InDedicatedSystemMemoryBytes);
		DedicatedVideoMemoryBytesMap.Add(TEXT("Untracked Memory"), InDedicatedVideoMemoryBytes);
	}

	/**
	 * Construct from legacy unknown size.
	 * Deliberately explicit to avoid accidental use.
	 */
	FResourceSizeEx(const EResourceSizeMode::Type InResourceSizeMode, const SIZE_T InUnknownMemoryBytes)
		: ResourceSizeMode(InResourceSizeMode)
	{
		UnknownMemoryBytesMap.Add(TEXT("Untracked Memory"), InUnknownMemoryBytes);
	}

	void LogSummary(FOutputDevice& Ar) const
	{
		auto PrintPair = [&Ar](const TPair<FName, SIZE_T>& Pair)
		{
			Ar.Logf(
				TEXT("%140s %15.2f"),
				*Pair.Key.ToString(),
				static_cast<double>(Pair.Value) / 1024.0
			);
		};

		for (const TPair<FName, SIZE_T>& Pair : DedicatedSystemMemoryBytesMap)
		{
			PrintPair(Pair);
		}
		for (const TPair<FName, SIZE_T>& Pair : DedicatedVideoMemoryBytesMap)
		{
			PrintPair(Pair);
		}
		for (const TPair<FName, SIZE_T>& Pair : UnknownMemoryBytesMap)
		{
			PrintPair(Pair);
		}
	}

	/**
	 * Get the type of resource size held in this struct.
	 */
	EResourceSizeMode::Type GetResourceSizeMode() const
	{
		return ResourceSizeMode;
	}

	FResourceSizeEx& AddDedicatedSystemMemoryBytes(const FName& Tag, const SIZE_T InMemoryBytes)
	{
		SIZE_T& CurrentSize = DedicatedSystemMemoryBytesMap.FindOrAdd(Tag);
		CurrentSize += InMemoryBytes;
		return *this;
	}

	/**
	 * Add the given number of bytes to the dedicated system memory count.
	 * @see DedicatedSystemMemoryBytes for a description of that memory type.
	 */
	FResourceSizeEx& AddDedicatedSystemMemoryBytes(const SIZE_T InMemoryBytes)
	{
		SIZE_T& CurrentSize = DedicatedSystemMemoryBytesMap.FindOrAdd(TEXT("Untracked Memory"));
		CurrentSize += InMemoryBytes;
		return *this;
	}

	/**
	 * Get the number of bytes allocated from dedicated system memory.
	 * @see DedicatedSystemMemoryBytes for a description of that memory type.
	 */
	SIZE_T GetDedicatedSystemMemoryBytes() const
	{
		SIZE_T Sum = 0;
		TArray<SIZE_T> Values;
		DedicatedSystemMemoryBytesMap.GenerateValueArray(Values);
		for (const SIZE_T s : Values) Sum += s;
		return Sum;
	}

	FResourceSizeEx& AddDedicatedVideoMemoryBytes(const FName& Tag, const SIZE_T InMemoryBytes)
	{
		SIZE_T& CurrentSize = DedicatedVideoMemoryBytesMap.FindOrAdd(Tag);
		CurrentSize += InMemoryBytes;
		return *this;
	}

	/**
	 * Add the given number of bytes to the dedicated video memory count.
	 * @see DedicatedVideoMemoryBytes for a description of that memory type.
	 */
	FResourceSizeEx& AddDedicatedVideoMemoryBytes(const SIZE_T InMemoryBytes)
	{
		SIZE_T& CurrentSize = DedicatedVideoMemoryBytesMap.FindOrAdd(TEXT("Untracked Memory"));
		CurrentSize += InMemoryBytes;
		return *this;
	}

	/**
	 * Get the number of bytes allocated from dedicated video memory.
	 * @see DedicatedVideoMemoryBytes for a description of that memory type.
	 */
	SIZE_T GetDedicatedVideoMemoryBytes() const
	{
		SIZE_T Sum = 0;
		TArray<SIZE_T> Values;
		DedicatedVideoMemoryBytesMap.GenerateValueArray(Values);
		for (const SIZE_T s : Values) Sum += s;
		return Sum;
	}

	FResourceSizeEx& AddUnknownMemoryBytes(const FName& Tag, const SIZE_T InMemoryBytes)
	{
		SIZE_T& CurrentSize = UnknownMemoryBytesMap.FindOrAdd(Tag);
		CurrentSize += InMemoryBytes;
		return *this;
	}

	/**
	 * Add the given number of bytes to the unknown memory count.
	 * @see UnknownMemoryBytes for a description of that memory type.
	 */
	FResourceSizeEx& AddUnknownMemoryBytes(const SIZE_T InMemoryBytes)
	{
		SIZE_T& CurrentSize = UnknownMemoryBytesMap.FindOrAdd(TEXT("Untracked Memory"));
		CurrentSize += InMemoryBytes;
		return *this;
	}

	/**
	 * Get the number of bytes allocated from unknown memory.
	 * @see UnknownMemoryBytes for a description of that memory type.
	 */
	SIZE_T GetUnknownMemoryBytes() const
	{
		SIZE_T Sum = 0;
		TArray<SIZE_T> Values;
		UnknownMemoryBytesMap.GenerateValueArray(Values);
		for (const SIZE_T s : Values) Sum += s;
		return Sum;
	}

	/**
	 * Get the total number of bytes allocated from any memory.
	 */
	SIZE_T GetTotalMemoryBytes() const
	{
		return GetDedicatedSystemMemoryBytes() + GetDedicatedVideoMemoryBytes() + GetUnknownMemoryBytes();
	}

	/**
	 * Add another FResourceSizeEx to this one.
	 */
	FResourceSizeEx& operator+=(const FResourceSizeEx& InRHS)
	{
		ensureAlwaysMsgf(ResourceSizeMode == InRHS.ResourceSizeMode, TEXT("The two resource sizes use different counting modes. The result of adding them together may be incorrect."));

		for (const TPair<FName, SIZE_T>& Pair : InRHS.DedicatedSystemMemoryBytesMap)
		{
			DedicatedSystemMemoryBytesMap.FindOrAdd(Pair.Key) += Pair.Value;
		}

		for (const TPair<FName, SIZE_T>& Pair : InRHS.DedicatedVideoMemoryBytesMap)
		{
			DedicatedVideoMemoryBytesMap.FindOrAdd(Pair.Key) += Pair.Value;
		}

		for (const TPair<FName, SIZE_T>& Pair : InRHS.UnknownMemoryBytesMap)
		{
			UnknownMemoryBytesMap.FindOrAdd(Pair.Key) += Pair.Value;
		}

		return *this;
	}

	/**
	 * Add two FResourceSizeEx instances together and return a copy.
	 */
	friend FResourceSizeEx operator+(FResourceSizeEx InLHS, const FResourceSizeEx& InRHS)
	{
		InLHS += InRHS;
		return InLHS;
	}

private:
	/**
	 * Type of resource size held in this struct.
	 */
	EResourceSizeMode::Type ResourceSizeMode;

	/**
	 * The number of bytes of memory that this resource is using for CPU resources that have been allocated from dedicated system memory.
	 * On platforms with unified memory, this typically refers to the things allocated in the preferred memory for CPU use.
	 */
	TMap<FName, SIZE_T> DedicatedSystemMemoryBytesMap;

	/**
	 * The number of bytes of memory that this resource is using for GPU resources that have been allocated from dedicated video memory.
	 * On platforms with unified memory, this typically refers to the things allocated in the preferred memory for GPU use.
	 */
	TMap<FName, SIZE_T> DedicatedVideoMemoryBytesMap;

	/**
	 * The number of bytes of memory that this resource is using from an unspecified section of memory.
	 * This exists so that the legacy GetResourceSize(...) functions can still report back memory usage until they're updated to use FResourceSizeEx, and should not be used in new memory tracking code.
	 */
	TMap<FName, SIZE_T> UnknownMemoryBytesMap;
};
