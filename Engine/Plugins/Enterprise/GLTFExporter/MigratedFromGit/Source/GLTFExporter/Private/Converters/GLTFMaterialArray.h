// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFHashableArray.h"

class UMaterialInterface;

struct FGLTFMaterialArray : FGLTFHashableArray<const UMaterialInterface*>
{
	using FGLTFHashableArray::FGLTFHashableArray;
	using FGLTFHashableArray::operator=;
	using FGLTFHashableArray::operator==;
	using FGLTFHashableArray::operator!=;

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
