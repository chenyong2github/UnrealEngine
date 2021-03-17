// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DataLayer.cpp: UDataLayer class implementation
=============================================================================*/

#include "WorldPartition/DataLayer/DataLayer.h"

#define LOCTEXT_NAMESPACE "DataLayer"

UDataLayer::UDataLayer(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
, DataLayerLabel(GetFName())
, InitialState(EDataLayerState::Unloaded)
, bIsVisible(true)
, bIsDynamicallyLoaded(false)
, bIsInitiallyActive_DEPRECATED(false)
, bIsDynamicallyLoadedInEditor(true)
, bGeneratesHLODs(false)
, DefaultHLODLayer()
#endif
{
}

void UDataLayer::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	if (bIsInitiallyActive_DEPRECATED)
	{
		InitialState = EDataLayerState::Activated;
	}
#endif
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

FText UDataLayer::GetDataLayerText(const UDataLayer* InDataLayer)
{
	return InDataLayer ? FText::FromName(InDataLayer->GetDataLayerLabel()) : LOCTEXT("InvalidDataLayerLabel", "<None>");
}

#endif

#undef LOCTEXT_NAMESPACE