// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"

struct GLTFEXPORTER_API FGLTFJsonSkin : IGLTFJsonIndexedObject
{
	FString Name;

	FGLTFJsonAccessorIndex InverseBindMatrices;
	FGLTFJsonNodeIndex Skeleton;

	TArray<FGLTFJsonNodeIndex> Joints;

	FGLTFJsonSkin(int32 Index = INDEX_NONE)
		: IGLTFJsonIndexedObject(Index)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
