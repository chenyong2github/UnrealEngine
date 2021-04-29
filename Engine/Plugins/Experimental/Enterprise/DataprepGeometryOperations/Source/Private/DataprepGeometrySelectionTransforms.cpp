// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataprepGeometrySelectionTransforms.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstance.h"
#include "JacketingProcess.h"

#define LOCTEXT_NAMESPACE "DataprepGeometrySelectionTransforms"

void UDataprepOverlappingActorsSelectionTransform::OnExecution_Implementation(const TArray<UObject*>& InObjects, TArray<UObject*>& OutObjects)
{
	TSet<AActor*> TargetActors;
	UWorld* World = nullptr;

	for (UObject* Object : InObjects)
	{
		if (!ensure(Object) || Object->IsPendingKill())
		{
			continue;
		}

		if (AActor* Actor = Cast< AActor >(Object))
		{
			if (World == nullptr)
			{
				World = Actor->GetWorld();
			}
			TargetActors.Add(Actor);
		}
	}

	if (World == nullptr || TargetActors.Num() == 0)
	{
		return;
	}

	TArray<AActor*> WorldActors;

	// Get all world actors that we want to test against our input set.
	for (ULevel* Level : World->GetLevels())
	{
		for (AActor* Actor : Level->Actors)
		{
			if (Actor)
			{
				// Skip actors that are present in the input.
				if (TargetActors.Contains(Actor) || Actor->IsPendingKillOrUnreachable())
				{
					continue;
				}
				WorldActors.Add(Actor);
			}
		}
	}

	// Run the overlap test.
	TArray<AActor*> OverlappingActors;

	FJacketingOptions Options;
	Options.Accuracy = Accuracy;
	Options.MergeDistance = MergeDistance;
	Options.Target = EJacketingTarget::Level;

	FJacketingProcess::FindOverlappingActors(WorldActors, TargetActors.Array(), &Options, OverlappingActors, true);
	
	OutObjects.Append(OverlappingActors);
}

#undef LOCTEXT_NAMESPACE
