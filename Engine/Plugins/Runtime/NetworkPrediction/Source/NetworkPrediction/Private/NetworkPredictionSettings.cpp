// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkPredictionSettings.h"
#include "NetworkPredictionWorldManager.h"
#include "UObject/UObjectIterator.h"

#if WITH_EDITOR
void UNetworkPredictionSettingsObject::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	for (TObjectIterator<UNetworkPredictionWorldManager> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			It->SyncNetworkPredictionSettings(this);
		}
	}
};
#endif