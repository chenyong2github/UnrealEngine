// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DataLayer.cpp: UDataLayer class implementation
=============================================================================*/

#include "WorldPartition/DataLayer/DataLayer.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"

#define LOCTEXT_NAMESPACE "DataLayer"

UDataLayer::UDataLayer(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
, bIsInitiallyActive_DEPRECATED(false)
, bIsVisible(true)
, bIsInitiallyVisible(true)
, bIsDynamicallyLoadedInEditor(true)
, bIsLocked(false)
#endif
, DataLayerLabel(GetFName())
, InitialState(EDataLayerState::Unloaded)
, bIsDynamicallyLoaded(false)
, DebugColor(FColor::Black)
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

	// Sanitize Label
	DataLayerLabel = UDataLayer::GetSanitizedDataLayerLabel(DataLayerLabel);

	if (DebugColor == FColor::Black)
	{
		FRandomStream RandomStream(GetFName());
		const uint8 R = (uint8)(RandomStream.GetFraction() * 255.f);
		const uint8 G = (uint8)(RandomStream.GetFraction() * 255.f);
		const uint8 B = (uint8)(RandomStream.GetFraction() * 255.f);
		DebugColor = FColor(R, G, B);
	}
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

FName UDataLayer::GetSanitizedDataLayerLabel(FName InDataLayerLabel)
{
	// Removes all quotes as well as whitespace characters from the startand end
	return FName(InDataLayerLabel.ToString().TrimStartAndEnd().Replace(TEXT("\""), TEXT("")));
}

#if WITH_EDITOR
void UDataLayer::SetDataLayerLabel(FName InDataLayerLabel)
{
	FName DataLayerLabelSanitized = UDataLayer::GetSanitizedDataLayerLabel(InDataLayerLabel);
	if (DataLayerLabel != DataLayerLabelSanitized)
	{
		Modify();
		AWorldDataLayers* WorldDataLayers = GetOuterAWorldDataLayers();
		check(!WorldDataLayers || !WorldDataLayers->GetDataLayerFromLabel(DataLayerLabelSanitized))
		DataLayerLabel = DataLayerLabelSanitized;
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