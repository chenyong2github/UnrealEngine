// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimeOfDayActorFactory.h"
#include "TimeOfDayActor.h"
#include "LevelSequence.h"

#define LOCTEXT_NAMESPACE "TimeOfDayEditor"

UTimeOfDayActorFactory::UTimeOfDayActorFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DisplayName = LOCTEXT("TimeOfDayDisplayName", "Time of Day");
	NewActorClass = ATimeOfDayActor::StaticClass();
}

bool UTimeOfDayActorFactory::CanCreateActorFrom( const FAssetData& AssetData, FText& OutErrorMsg )
{
	if (UActorFactory::CanCreateActorFrom(AssetData, OutErrorMsg))
	{
		return true;
	}

	if (AssetData.IsValid() && !AssetData.IsInstanceOf(ULevelSequence::StaticClass()))
	{
		OutErrorMsg = NSLOCTEXT("CanCreateActor", "NoLevelSequenceAsset", "A valid sequencer asset must be specified.");
		return false;
	}
	
	return true;
}

AActor* UTimeOfDayActorFactory::SpawnActor(UObject* InAsset, ULevel* InLevel, const FTransform& InTransform, const FActorSpawnParameters& InSpawnParams)
{
	ATimeOfDayActor* NewActor = Cast<ATimeOfDayActor>(Super::SpawnActor(InAsset, InLevel, InTransform, InSpawnParams));
	if (NewActor)
	{
		if (ULevelSequence* LevelSequence = Cast<ULevelSequence>(InAsset))
		{
			NewActor->SetDaySequence(LevelSequence);
		}
	}
	return NewActor;
}

UObject* UTimeOfDayActorFactory::GetAssetFromActorInstance(AActor* Instance)
{
	if (const ATimeOfDayActor* TimeOfDayActor = Cast<ATimeOfDayActor>(Instance))
	{
		return TimeOfDayActor->GetDaySequence();
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE

