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

	const UMaterialInterface* GetOverride(const TArray<UMaterialInterface*>& Originals, int32 Index) const
	{
		return GetOverride<UMaterialInterface*>(Originals, Index);
	}

	TArray<UMaterialInterface*> GetOverrides(const TArray<FStaticMaterial>& Originals) const
	{
		return GetOverrides<FStaticMaterial>(Originals);
	}

	TArray<UMaterialInterface*> GetOverrides(const TArray<FSkeletalMaterial>& Originals) const
	{
		return GetOverrides<FSkeletalMaterial>(Originals);
	}

	TArray<UMaterialInterface*> GetOverrides(const TArray<UMaterialInterface*>& Originals) const
	{
		return GetOverrides<UMaterialInterface*>(Originals);
	}

	void ClearRedundantOverrides(const TArray<FStaticMaterial>& Originals)
	{
		ClearRedundantOverrides<FStaticMaterial>(Originals);
	}

	void ClearRedundantOverrides(const TArray<FSkeletalMaterial>& Originals)
	{
		ClearRedundantOverrides<FSkeletalMaterial>(Originals);
	}

	void ClearRedundantOverrides(const TArray<UMaterialInterface*>& Originals)
	{
		ClearRedundantOverrides<UMaterialInterface*>(Originals);
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

	bool operator==(const TArray<UMaterialInterface*>& Other) const
	{
		return Equals(Other);
	}

	bool operator!=(const TArray<UMaterialInterface*>& Other) const
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
			return GetInterface(Originals[Index]);
		}

		return nullptr;
	}

	template <typename MeshMaterialType>
	TArray<UMaterialInterface*> GetOverrides(const TArray<MeshMaterialType>& Originals) const
	{
		const int32 Count = Originals.Num();
		TArray<UMaterialInterface*> Materials;
		Materials.AddDefaulted(Count);

		for (int32 Index = 0; Index < Count; ++Index)
		{
			Materials[Index] = const_cast<UMaterialInterface*>(GetOverride(Originals, Index));
		}

		return Materials;
	}

	template <typename MeshMaterialType>
	void ClearRedundantOverrides(const TArray<MeshMaterialType>& Originals)
	{
		const int32 Count = Originals.Num();
		SetNumZeroed(Count);

		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (GetInterface(Originals[Index]) == (*this)[Index])
			{
				(*this)[Index] = nullptr;
			}
		}
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
			if (!Equals(Other[Index], Index))
			{
				return false;
			}
		}

		return true;
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

	template <typename MeshMaterialType>
	bool Equals(const MeshMaterialType& Other, int32 Index) const
	{
		return IsValidIndex(Index) && GetInterface(Other) == (*this)[Index];
	}
};
