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

	void FillIn(const TArray<UMaterialInterface*>& Defaults)
	{
		return FillIn<UMaterialInterface*>(Defaults);
	}

	void FillIn(const TArray<FStaticMaterial>& Defaults)
	{
		return FillIn<FStaticMaterial>(Defaults);
	}

	void FillIn(const TArray<FSkeletalMaterial>& Defaults)
	{
		return FillIn<FSkeletalMaterial>(Defaults);
	}

	using TArray::operator=;
	using TArray::operator==;
	using TArray::operator!=;

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
	void FillIn(const TArray<MeshMaterialType>& Defaults)
	{
		const int32 Count = Defaults.Num();
		SetNumZeroed(Count);

		for (int32 Index = 0; Index < Count; ++Index)
		{
			const UMaterialInterface*& Element = (*this)[Index];
			if (Element == nullptr)
			{
				Element = GetInterface(Defaults[Index]);
				if (Element == nullptr)
				{
					Element = UMaterial::GetDefaultMaterial(MD_Surface);
				}
			}
		}
	}

	static const UMaterialInterface* GetInterface(const UMaterialInterface* Material)
	{
		return Material;
	}

	static const UMaterialInterface* GetInterface(const FStaticMaterial& Material)
	{
		return Material.MaterialInterface;
	}

	static const UMaterialInterface* GetInterface(const FSkeletalMaterial& Material)
	{
		return Material.MaterialInterface;
	}
};
