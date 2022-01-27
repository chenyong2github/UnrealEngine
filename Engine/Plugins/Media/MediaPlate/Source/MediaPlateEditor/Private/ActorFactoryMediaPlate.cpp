// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorFactoryMediaPlate.h"
#include "MediaSource.h"
#include "MediaPlate.h"

#define LOCTEXT_NAMESPACE "ActorFactoryMediaPlate"

UActorFactoryMediaPlate::UActorFactoryMediaPlate(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DisplayName = LOCTEXT("MediaPlateDisplayName", "Media Plate");
	NewActorClass = AMediaPlate::StaticClass();
}

bool UActorFactoryMediaPlate::CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg)
{
	if (AssetData.IsValid())
	{
		UClass* AssetClass = AssetData.GetClass();
		if ((AssetClass != nullptr) && (AssetClass->IsChildOf(UMediaSource::StaticClass())))
		{
			return true;
		}
		else
		{
			return false;
		}
	}
	else
	{
		return true;
	}
}

#undef LOCTEXT_NAMESPACE
