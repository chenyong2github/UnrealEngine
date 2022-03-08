// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/DataLayer/DataLayerEditorContext.h"

#if WITH_EDITOR

#include "WorldPartition/DataLayer/DataLayer.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "Engine/World.h"

/*
 * FDataLayerEditorContext
 */
FDataLayerEditorContext::FDataLayerEditorContext(UWorld* InWorld, const TArray<FName>& InDataLayers)
	: Hash(FDataLayerEditorContext::EmptyHash)
{
	const AWorldDataLayers* WorldDataLayers = InWorld->GetWorldDataLayers();
	if (!WorldDataLayers)
	{
		return;
	}

	for (const FName& DataLayerName : InDataLayers)
	{
		if (const UDataLayer* DataLayerObject = WorldDataLayers->GetDataLayerFromName(DataLayerName))
		{
			DataLayers.AddUnique(DataLayerObject->GetFName());
		}
	}

	if (DataLayers.Num())
	{
		DataLayers.Sort([](const FName& A, const FName& B) { return A.ToString() < B.ToString(); });
		for (FName LayerName : DataLayers)
		{
			Hash = FCrc::StrCrc32(*LayerName.ToString(), Hash);
		}
		check(Hash != FDataLayerEditorContext::EmptyHash);
	}
}

#endif