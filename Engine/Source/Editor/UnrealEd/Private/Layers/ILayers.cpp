// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Layers/ILayers.h"
#include "Layers/Layer.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Operations on Levels
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ILayers::AddLevelLayerInformation(const TWeakObjectPtr< ULevel >& Level)
{
	if (Level.IsValid())
	{
		AddLevelLayerInformation(Level.Get());
	}
}

void ILayers::RemoveLevelLayerInformation(const TWeakObjectPtr< ULevel >& Level)
{
	if (Level.IsValid())
	{
		RemoveLevelLayerInformation(Level.Get());
	}
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Operations on an individual actor.
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ILayers::IsActorValidForLayer(const TWeakObjectPtr< AActor >& Actor)
{
	if (Actor.IsValid())
	{
		return IsActorValidForLayer(Actor.Get());
	}
	return false;
}

bool ILayers::InitializeNewActorLayers(const TWeakObjectPtr< AActor >& Actor)
{
	if (Actor.IsValid())
	{
		return InitializeNewActorLayers(Actor.Get());
	}
	return false;
}

bool ILayers::DisassociateActorFromLayers(const TWeakObjectPtr< AActor >& Actor)
{
	if (Actor.IsValid())
	{
		return DisassociateActorFromLayers(Actor.Get());
	}
	return false;
}


bool ILayers::AddActorToLayer(const TWeakObjectPtr< AActor >& Actor, const FName& LayerName)
{
	return AddActorToLayer(Actor.Get(), LayerName);
}

bool ILayers::AddActorToLayers(const TWeakObjectPtr< AActor >& Actor, const TArray< FName >& LayerNames)
{
	return AddActorToLayers(Actor.Get(), LayerNames);
}


bool ILayers::RemoveActorFromLayer(const TWeakObjectPtr< AActor >& Actor, const FName& LayerName, const bool bUpdateStats)
{
	return RemoveActorFromLayer(Actor.Get(), LayerName, bUpdateStats);
}

bool ILayers::RemoveActorFromLayers(const TWeakObjectPtr< AActor >& Actor, const TArray< FName >& LayerNames, const bool bUpdateStats)
{
	return RemoveActorFromLayers(Actor.Get(), LayerNames, bUpdateStats);
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Operations on actor viewport visibility regarding layers
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ILayers::UpdateActorAllViewsVisibility(const TWeakObjectPtr< AActor >& Actor)
{
	UpdateActorAllViewsVisibility(Actor.Get());
}


void ILayers::UpdateActorViewVisibility(FLevelEditorViewportClient* ViewportClient, const TWeakObjectPtr< AActor >& Actor, bool bReregisterIfDirty)
{
	UpdateActorViewVisibility(ViewportClient, Actor.Get(), bReregisterIfDirty);
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Operations on actor viewport visibility regarding layers
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ILayers::UpdateActorVisibility(const TWeakObjectPtr< AActor >& Actor, bool& bOutSelectionChanged, bool& bOutActorModified, bool bNotifySelectionChange, bool bRedrawViewports)
{
	return UpdateActorVisibility(Actor.Get(), bOutSelectionChanged, bOutActorModified, bNotifySelectionChange, bRedrawViewports);
}

bool ILayers::TryGetLayer(const FName& LayerName, TWeakObjectPtr< ULayer >& OutLayer)
{
	ULayer* OutLayerRawPtr;
	const bool result = TryGetLayer(LayerName, OutLayerRawPtr);
	OutLayer = OutLayerRawPtr;
	return result;
}

void ILayers::AppendActorsForLayer(const FName& LayerName, TArray< TWeakObjectPtr< AActor > >& InOutActors, const TSharedPtr< ActorFilter >& Filter) const
{
	AppendActorsFromLayer(LayerName, InOutActors, Filter);
}

void ILayers::AppendActorsForLayers(const TArray< FName >& LayerNames, TArray< TWeakObjectPtr< AActor > >& InOutActors, const TSharedPtr< ActorFilter >& Filter) const
{
	AppendActorsFromLayers(LayerNames, InOutActors, Filter);
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS
