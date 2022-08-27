// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

// The reason behind a custom material array type are two:
// 1. Forwarding a TArray by copy in FGLTFConverter::GetOrAdd twice will result in a empty array
// 2. There is no GetTypeHash for generic arrays
struct FGLTFMaterialArray : TArray<const UMaterialInterface*>
{
	FGLTFMaterialArray()
	{
	}

	FGLTFMaterialArray(const TArray& Other)
		: TArray(Other)
	{
	}

	FGLTFMaterialArray(const FGLTFMaterialArray& Other)
        : TArray(Other)
	{
	}

	template <typename ElementType, typename AllocatorType>
    explicit FGLTFMaterialArray(const TArray<ElementType, AllocatorType>& Other)
        : TArray(Other)
	{
	}

	FGLTFMaterialArray(std::initializer_list<const UMaterialInterface*> InitList)
		: TArray(InitList)
	{
	}

	FGLTFMaterialArray& operator=(std::initializer_list<const UMaterialInterface*> InitList)
	{
		static_cast<TArray&>(*this) = InitList;
		return *this;
	}

	FGLTFMaterialArray& operator=(const TArray& Other)
	{
		static_cast<TArray&>(*this) = Other;
		return *this;
	}

	template <typename ElementType, typename AllocatorType>
    FGLTFMaterialArray& operator=(const TArray<ElementType, AllocatorType>& Other)
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
		for (const UMaterialInterface* Material : Array)
		{
			Hash = HashCombine(Hash, GetTypeHash(Material));
		}
		return Hash;
	}
};
