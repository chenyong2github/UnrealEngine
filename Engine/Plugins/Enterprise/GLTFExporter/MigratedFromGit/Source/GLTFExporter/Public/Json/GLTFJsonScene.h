// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"

struct GLTFEXPORTER_API FGLTFJsonScene : IGLTFJsonIndexedObject
{
	FString Name;

	TArray<FGLTFJsonNodeIndex> Nodes;
	TArray<FGLTFJsonEpicLevelVariantSetsIndex>  EpicLevelVariantSets;

	FGLTFJsonScene(int32 Index = INDEX_NONE)
		: IGLTFJsonIndexedObject(Index)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
