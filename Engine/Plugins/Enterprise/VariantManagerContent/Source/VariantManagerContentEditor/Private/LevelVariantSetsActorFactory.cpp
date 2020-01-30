// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelVariantSetsActorFactory.h"

#include "LevelVariantSets.h"
#include "LevelVariantSetsActor.h"

#include "ActorFactories/ActorFactory.h"
#include "AssetData.h"

#define LOCTEXT_NAMESPACE "ALevelVariantSetsActorFactory"

ULevelVariantSetsActorFactory::ULevelVariantSetsActorFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DisplayName = LOCTEXT("ALevelVariantSetsActorDisplayName", "LevelVariantSetsActor");
	NewActorClass = ALevelVariantSetsActor::StaticClass();
}

bool ULevelVariantSetsActorFactory::CanCreateActorFrom( const FAssetData& AssetData, FText& OutErrorMsg )
{
	if ( UActorFactory::CanCreateActorFrom( AssetData, OutErrorMsg ) )
	{
		return true;
	}

	if ( AssetData.IsValid() && !AssetData.GetClass()->IsChildOf( ULevelVariantSets::StaticClass() ) )
	{
		OutErrorMsg = NSLOCTEXT("CanCreateActor", "NoLevelVariantSetsAsset", "A valid variant sets asset must be specified.");
		return false;
	}

	return true;
}

AActor* ULevelVariantSetsActorFactory::SpawnActor( UObject* Asset, ULevel* InLevel, const FTransform& Transform, EObjectFlags InObjectFlags, const FName Name )
{
	ALevelVariantSetsActor* NewActor = Cast<ALevelVariantSetsActor>(Super::SpawnActor(Asset, InLevel, Transform, InObjectFlags, Name));

	if (NewActor)
	{
		if (ULevelVariantSets* LevelVariantSets = Cast<ULevelVariantSets>(Asset))
		{
			NewActor->SetLevelVariantSets(LevelVariantSets);
		}
	}

	return NewActor;
}

UObject* ULevelVariantSetsActorFactory::GetAssetFromActorInstance(AActor* Instance)
{
	if (ALevelVariantSetsActor* LevelVariantSetsActor = Cast<ALevelVariantSetsActor>(Instance))
	{
		return LevelVariantSetsActor->LevelVariantSets.TryLoad();
	}

	return nullptr;
}

#undef LOCTEXT_NAMESPACE