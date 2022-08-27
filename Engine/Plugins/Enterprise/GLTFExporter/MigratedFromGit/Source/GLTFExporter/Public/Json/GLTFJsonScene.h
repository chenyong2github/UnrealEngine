// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonIndex.h"

struct FGLTFJsonScene : IGLTFJsonObject
{
	FString Name;

	TArray<FGLTFJsonNodeIndex> Nodes;
	TArray<FGLTFJsonEpicLevelVariantSetsIndex>  EpicLevelVariantSets;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		if (!Name.IsEmpty())
		{
			Writer.Write(TEXT("name"), Name);
		}

		if (Nodes.Num() > 0)
		{
			Writer.Write(TEXT("nodes"), Nodes);
		}

		if (EpicLevelVariantSets.Num() > 0)
		{
			Writer.StartExtensions();

			Writer.StartExtension(EGLTFJsonExtension::EPIC_LevelVariantSets);
			Writer.Write(TEXT("levelVariantSets"), EpicLevelVariantSets);
			Writer.EndExtension();

			Writer.EndExtensions();
		}
	}
};
