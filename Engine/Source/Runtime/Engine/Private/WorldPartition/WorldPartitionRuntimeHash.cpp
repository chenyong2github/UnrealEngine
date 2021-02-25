// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODActorDesc.h"
#include "Engine/LevelScriptBlueprint.h"
#include "ActorReferencesUtils.h"

UWorldPartitionRuntimeHash::UWorldPartitionRuntimeHash(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{}

#if WITH_EDITOR
void UWorldPartitionRuntimeHash::OnPreBeginPIE()
{
	// Mark always loaded actors so that the Level will force reference to these actors for PIE.
	// These actor will then be duplicated for PIE during the PIE world duplication process
	ForceExternalActorLevelReference(/*bForceExternalActorLevelReferenceForPIE*/true);
}

void UWorldPartitionRuntimeHash::OnEndPIE()
{
	// Unmark always loaded actors
	ForceExternalActorLevelReference(/*bForceExternalActorLevelReferenceForPIE*/false);

	// Release references (will unload actors that were not already loaded in the Editor)
	AlwaysLoadedActorsForPIE.Empty();
}

void UWorldPartitionRuntimeHash::ForceExternalActorLevelReference(bool bForceExternalActorLevelReferenceForPIE)
{
	for (const FWorldPartitionReference& AlwaysLoadedActor : AlwaysLoadedActorsForPIE)
	{
		if (AActor* Actor = (*AlwaysLoadedActor)->GetActor())
		{
			Actor->SetForceExternalActorLevelReferenceForPIE(bForceExternalActorLevelReferenceForPIE);
		}
	}
}

void UWorldPartitionRuntimeHash::CreateActorDescViewMap(const UActorDescContainer* Container, TMap<FGuid, FWorldPartitionActorDescView>& OutActorDescViewMap) const
{
	// Build the actor desc view map
	OutActorDescViewMap.Empty();
	for (UActorDescContainer::TConstIterator<> ActorDescIt(Container); ActorDescIt; ++ActorDescIt)
	{
		OutActorDescViewMap.Emplace(ActorDescIt->GetGuid(), *ActorDescIt);
	}

	// Set HLOD parents into actor desc views
	for (UActorDescContainer::TConstIterator<AWorldPartitionHLOD> HLODIterator(Container); HLODIterator; ++HLODIterator)
	{
		for (const FGuid& SubActor : HLODIterator->GetSubActors())
		{
			if (FWorldPartitionActorDescView* SubActorDescView = OutActorDescViewMap.Find(SubActor))
			{
				SubActorDescView->SetHLODParent(HLODIterator->GetGuid());
			}
		}
	}

	// Gather all references to external actors from the level script and make them always loaded
	if (ULevelScriptBlueprint* LevelScriptBlueprint = Container->GetWorld()->PersistentLevel->GetLevelScriptBlueprint(true))
	{
		TArray<AActor*> LevelScriptExternalActorReferences = ActorsReferencesUtils::GetExternalActorReferences(LevelScriptBlueprint);

		for (AActor* Actor : LevelScriptExternalActorReferences)
		{
			if (FWorldPartitionActorDescView* ActorDescView = OutActorDescViewMap.Find(Actor->GetActorGuid()))
			{
				ChangeActorDescViewGridPlacement(*ActorDescView, EActorGridPlacement::AlwaysLoaded);
			}
		}
	}
}

void UWorldPartitionRuntimeHash::ChangeActorDescViewGridPlacement(FWorldPartitionActorDescView& ActorDescView, EActorGridPlacement GridPlacement) const
{
	if (ActorDescView.EffectiveGridPlacement != GridPlacement)
	{
		ActorDescView.EffectiveGridPlacement = GridPlacement;
		UE_LOG(LogWorldPartition, Warning, TEXT("Actor '%s' grid placement changed to %d"), *ActorDescView.GetActorLabel().ToString(), (int32)GridPlacement);
	}
}
#endif
