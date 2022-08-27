// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

template <typename ElementType>
struct FGLTFHashableArray : TArray<ElementType>
{
	FGLTFHashableArray()
	{
	}

	FGLTFHashableArray(const TArray<ElementType>& Other)
		: TArray(Other)
	{
	}

	FGLTFHashableArray(const FGLTFHashableArray& Other)
        : TArray(Other)
	{
	}

	template <typename OtherElementType, typename OtherAllocatorType>
	explicit FGLTFHashableArray(const TArray<OtherElementType, OtherAllocatorType>& Other)
		: TArray(Other)
	{
	}

	FGLTFHashableArray(std::initializer_list<ElementType> InitList)
		: TArray(InitList)
	{
	}

	using TArray<ElementType>::operator=;
	using TArray<ElementType>::operator==;
	using TArray<ElementType>::operator!=;

	friend uint32 GetTypeHash(const FGLTFHashableArray<ElementType>& Array)
	{
		uint32 Hash = GetTypeHash(Array.Num());
		for (const ElementType& Element : Array)
		{
			Hash = HashCombine(Hash, GetTypeHash(Element));
		}
		return Hash;
	}
};
