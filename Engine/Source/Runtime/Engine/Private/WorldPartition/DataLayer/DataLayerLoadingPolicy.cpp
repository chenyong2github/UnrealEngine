// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/DataLayer/DataLayerLoadingPolicy.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"

#if WITH_EDITOR
bool UDataLayerLoadingPolicy::ResolveIsLoadedInEditor(TArray<const UDataLayerInstance*>& InDataLayers) const
{
	check(!InDataLayers.IsEmpty());
	for (const UDataLayerInstance* DataLayerInstance : InDataLayers)
	{
		if (DataLayerInstance->IsEffectiveLoadedInEditor())
		{
			return true;
		}
	}
	return false;
}
#endif