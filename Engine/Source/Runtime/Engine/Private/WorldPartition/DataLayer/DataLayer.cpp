// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DataLayer.cpp: UDataLayer class implementation
=============================================================================*/

#include "WorldPartition/DataLayer/DataLayer.h"

#define LOCTEXT_NAMESPACE "DataLayer"

UDataLayer::UDataLayer(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
, bIsInitiallyActive_DEPRECATED(false)
, bIsVisible(true)
, bIsInitiallyVisible(true)
, bIsDynamicallyLoadedInEditor(true)
, bIsLocked(false)
, bGeneratesHLODs(false)
, DefaultHLODLayer()
#endif
, DataLayerLabel(GetFName())
, InitialState(EDataLayerState::Unloaded)
, bIsDynamicallyLoaded(false)
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

	// Initialize bIsVisible with persistent flag bIsInitiallyVisible
	bIsVisible = bIsInitiallyVisible;
#endif
}

bool UDataLayer::IsInitiallyVisible() const
{
#if WITH_EDITOR
	return bIsInitiallyVisible;
#else
	return false;
#endif
}

bool UDataLayer::IsVisible() const
{
#if WITH_EDITOR
	return bIsVisible;
#else
	return false;
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
		Modify(/*bAlwaysMarkDirty*/false);
		bIsVisible = bInIsVisible;
	}
}

void UDataLayer::SetIsInitiallyVisible(bool bInIsInitiallyVisible)
{
	if (bIsInitiallyVisible != bInIsInitiallyVisible)
	{
		Modify();
		bIsInitiallyVisible = bInIsInitiallyVisible;
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
	if (bIsDynamicallyLoadedInEditor != bInIsDynamicallyLoadedInEditor)
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