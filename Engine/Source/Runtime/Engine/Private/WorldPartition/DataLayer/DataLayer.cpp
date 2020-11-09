// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DataLayer.cpp: UDataLayer class implementation
=============================================================================*/

#include "WorldPartition/DataLayer/DataLayer.h"

UDataLayer::UDataLayer(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
, DataLayerLabel(GetFName())
, bIsVisible(true)
, bIsDynamicallyLoaded(true)
, bIsDynamicallyLoadedInEditor(true)
#endif
{
}

#if WITH_EDITOR
void UDataLayer::SetDataLayerLabel(FName InDataLayerLabel)
{
	if (DataLayerLabel != InDataLayerLabel)
	{
		Modify();
		AWorldDataLayers* WorldDataLayers = GetOuterAWorldDataLayers();
		check(!WorldDataLayers || !WorldDataLayers->GetDataLayerFromLabel(InDataLayerLabel))
		DataLayerLabel = InDataLayerLabel;
	}
}

void UDataLayer::SetVisible(bool bInIsVisible)
{
	if (bIsVisible != bInIsVisible)
	{
		Modify();
		bIsVisible = bInIsVisible;
	}
}

void UDataLayer::SetIsDynamicallyLoaded(bool bInIsDynamicallyLoaded)
{
	if (bIsDynamicallyLoaded != bInIsDynamicallyLoaded)
	{
		Modify();
		bIsDynamicallyLoaded = bInIsDynamicallyLoaded;
	}
}

void UDataLayer::SetIsDynamicallyLoadedInEditor(bool bInIsDynamicallyLoadedInEditor)
{
	if (bIsDynamicallyLoadedInEditor != bInIsDynamicallyLoadedInEditor && (!bInIsDynamicallyLoadedInEditor || bIsDynamicallyLoaded))
	{
		Modify(false);
		bIsDynamicallyLoadedInEditor = bInIsDynamicallyLoadedInEditor;
	}
}

#endif