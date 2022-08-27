// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"

struct GLTFEXPORTER_API FGLTFJsonScene : IGLTFJsonObject
{
	FString Name;

	TArray<FGLTFJsonNodeIndex> Nodes;
	TArray<FGLTFJsonEpicLevelVariantSetsIndex>  EpicLevelVariantSets;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
