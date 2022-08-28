// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// The reason behind a custom material array type are two:
// 1. Forwarding a TArray by copy in FGLTFConverter::GetOrAdd twice will result in a empty array
// 2. There is no GetTypeHash for generic arrays
struct FGLTFBoneMap : TArray<FBoneIndexType>
{
	FGLTFBoneMap()
	{
	}

	FGLTFBoneMap(const TArray& Other)
		: TArray(Other)
	{
	}

	FGLTFBoneMap(const FGLTFBoneMap& Other)
        : TArray(Other)
	{
	}

	template <typename ElementType, typename AllocatorType>
    explicit FGLTFBoneMap(const TArray<ElementType, AllocatorType>& Other)
        : TArray(Other)
	{
	}

	FGLTFBoneMap(std::initializer_list<FBoneIndexType> InitList)
		: TArray(InitList)
	{
	}

	FGLTFBoneMap& operator=(std::initializer_list<FBoneIndexType> InitList)
	{
		static_cast<TArray&>(*this) = InitList;
		return *this;
	}

	FGLTFBoneMap& operator=(const TArray& Other)
	{
		static_cast<TArray&>(*this) = Other;
		return *this;
	}

	template <typename ElementType, typename AllocatorType>
    FGLTFBoneMap& operator=(const TArray<ElementType, AllocatorType>& Other)
	{
		static_cast<TArray&>(*this) = Other;
		return *this;
	}

	bool operator==(const TArray& Other) const
	{
		return static_cast<const TArray&>(*this) == Other;
	}

	bool operator!=(const TArray& Other) const
	{
		return static_cast<const TArray&>(*this) != Other;
	}

	friend uint32 GetTypeHash(const TArray& Array)
	{
		uint32 Hash = GetTypeHash(Array.Num());
		for (FBoneIndexType Element : Array)
		{
			Hash = HashCombine(Hash, GetTypeHash(Element));
		}
		return Hash;
	}
};
