// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"

struct GLTFEXPORTER_API FGLTFJsonSkin : IGLTFJsonIndexedObject
{
	FString Name;

	FGLTFJsonAccessor* InverseBindMatrices;
	FGLTFJsonNode* Skeleton;

	TArray<FGLTFJsonNode*> Joints;

	FGLTFJsonSkin(int32 Index = INDEX_NONE)
		: IGLTFJsonIndexedObject(Index)
		, InverseBindMatrices(nullptr)
		, Skeleton(nullptr)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
