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

	const UMaterialInterface* GetOverride(const TArray<FStaticMaterial>& Originals, int32 Index) const
	{
		return GetOverride<FStaticMaterial>(Originals, Index);
	}

	const UMaterialInterface* GetOverride(const TArray<FSkeletalMaterial>& Originals, int32 Index) const
	{
		return GetOverride<FSkeletalMaterial>(Originals, Index);
	}

	using TArray::operator=;
	using TArray::operator==;
	using TArray::operator!=;

	bool operator==(const TArray<FStaticMaterial>& Other) const
	{
		return Equals(Other);
	}

	bool operator!=(const TArray<FStaticMaterial>& Other) const
	{
		return !Equals(Other);
	}

	bool operator==(const TArray<FSkeletalMaterial>& Other) const
	{
		return Equals(Other);
	}

	bool operator!=(const TArray<FSkeletalMaterial>& Other) const
	{
		return !Equals(Other);
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

private:

	template <typename MeshMaterialType>
    const UMaterialInterface* GetOverride(const TArray<MeshMaterialType>& Originals, int32 Index) const
	{
		if (IsValidIndex(Index) && (*this)[Index] != nullptr)
		{
			return (*this)[Index];
		}

		if (Originals.IsValidIndex(Index))
		{
			return Originals[Index].MaterialInterface;
		}

		return nullptr;
	}

	template <typename MeshMaterialType>
	bool Equals(const TArray<MeshMaterialType>& Other) const
	{
		if (Other.Num() != Num())
		{
			return false;
		}

		for (int32 Index = 0; Index < Num(); ++Index)
		{
			if (Other[Index].MaterialInterface != (*this)[Index])
			{
				return false;
			}
		}

		return true;
	}
};
