// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"

struct GLTFEXPORTER_API FGLTFJsonSkin : IGLTFJsonObject
{
	FString Name;

	FGLTFJsonAccessorIndex InverseBindMatrices;
	FGLTFJsonNodeIndex Skeleton;

	TArray<FGLTFJsonNodeIndex> Joints;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
