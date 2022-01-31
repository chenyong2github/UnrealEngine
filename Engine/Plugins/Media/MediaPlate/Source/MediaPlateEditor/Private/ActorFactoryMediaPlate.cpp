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

void UActorFactoryMediaPlate::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
	Super::PostSpawnActor(Asset, NewActor);

	SetUpActor(Asset, NewActor);
}

void UActorFactoryMediaPlate::PostCreateBlueprint(UObject* Asset, AActor* CDO)
{
	SetUpActor(Asset, CDO);
}

void UActorFactoryMediaPlate::SetUpActor(UObject* Asset, AActor* Actor)
{
	if (Actor != nullptr)
	{
		AMediaPlate* MediaPlate = CastChecked<AMediaPlate>(Actor);

		// Hook up media source.
		UMediaSource* MediaSource = Cast<UMediaSource>(Asset);
		if (MediaSource != nullptr)
		{
			MediaPlate->MediaSource = MediaSource;
		}
	}
}

#undef LOCTEXT_NAMESPACE
