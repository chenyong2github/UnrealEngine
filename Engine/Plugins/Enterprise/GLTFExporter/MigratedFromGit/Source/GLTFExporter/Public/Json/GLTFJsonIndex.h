// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

template <typename DerivedType>
struct FGLTFJsonIndex
{
	int32 Value;

	FGLTFJsonIndex()
		: Value(INDEX_NONE)
	{
	}

	explicit FGLTFJsonIndex(int32 Value)
		: Value(Value)
	{
	}

	FORCEINLINE operator int32() const
	{
		return Value;
	}

	FORCEINLINE bool operator==(int32 OtherValue) const
	{
		return Value == OtherValue;
	}

	FORCEINLINE bool operator!=(int32 OtherValue) const
	{
		return Value != OtherValue;
	}

	FORCEINLINE bool operator==(const DerivedType& Other) const
	{
		return Value == Other.Value;
	}

	FORCEINLINE bool operator!=(const DerivedType& Other) const
	{
		return Value != Other.Value;
	}

	FORCEINLINE friend uint32 GetTypeHash(const DerivedType& Other)
	{
		return GetTypeHash(Other.Value);
	}
};

struct FGLTFJsonAccessorIndex : FGLTFJsonIndex<FGLTFJsonAccessorIndex> { using FGLTFJsonIndex::FGLTFJsonIndex; };
struct FGLTFJsonBufferIndex : FGLTFJsonIndex<FGLTFJsonBufferIndex> { using FGLTFJsonIndex::FGLTFJsonIndex; };
struct FGLTFJsonBufferViewIndex : FGLTFJsonIndex<FGLTFJsonBufferViewIndex> { using FGLTFJsonIndex::FGLTFJsonIndex; };
struct FGLTFJsonCameraIndex : FGLTFJsonIndex<FGLTFJsonCameraIndex> { using FGLTFJsonIndex::FGLTFJsonIndex; };
struct FGLTFJsonMaterialIndex : FGLTFJsonIndex<FGLTFJsonMaterialIndex> { using FGLTFJsonIndex::FGLTFJsonIndex; };
struct FGLTFJsonMeshIndex : FGLTFJsonIndex<FGLTFJsonMeshIndex> { using FGLTFJsonIndex::FGLTFJsonIndex; };
struct FGLTFJsonNodeIndex : FGLTFJsonIndex<FGLTFJsonNodeIndex> { using FGLTFJsonIndex::FGLTFJsonIndex; };
struct FGLTFJsonSceneIndex : FGLTFJsonIndex<FGLTFJsonSceneIndex> { using FGLTFJsonIndex::FGLTFJsonIndex; };
struct FGLTFJsonSkinIndex : FGLTFJsonIndex<FGLTFJsonSkinIndex> { using FGLTFJsonIndex::FGLTFJsonIndex; };
