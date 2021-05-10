// Copyright Epic Games, Inc. All Rights Reserved.

#include "HLODBuilder.h"

#include "Engine/StaticMesh.h"

#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODLayer.h"

DEFINE_LOG_CATEGORY(LogHLODBuilder);


void FHLODBuilder::Build(AWorldPartitionHLOD* InHLODActor, const UHLODLayer* InHLODLayer, const TArray<FWorldPartitionReference>& InSubActors)
{
	TArray<UPrimitiveComponent*> SubComponents = GatherPrimitiveComponents(InSubActors);
	if (SubComponents.IsEmpty())
	{
		return;
	}

	TArray<UPrimitiveComponent*> HLODPrimitives = CreateComponents(InHLODActor, InHLODLayer, SubComponents);
	HLODPrimitives.RemoveSwap(nullptr);

	if (!HLODPrimitives.IsEmpty())
	{
		InHLODActor->Modify();
		InHLODActor->SetHLODPrimitives(HLODPrimitives);
	}
}

TArray<UPrimitiveComponent*> FHLODBuilder::GatherPrimitiveComponents(const TArray<FWorldPartitionReference>& InActors)
{
	TArray<UPrimitiveComponent*> PrimitiveComponents;

	auto GatherPrimitivesFromActor = [&PrimitiveComponents](const AActor* Actor, const AActor* ParentActor = nullptr)
	{
		const TCHAR* Padding = ParentActor ? TEXT("    ") : TEXT("");
		UE_LOG(LogHLODBuilder, Verbose, TEXT("%s* Adding components from actor %s"), Padding, *Actor->GetName());
		for (UActorComponent* SubComponent : Actor->GetComponents())
		{
			if (SubComponent && SubComponent->IsHLODRelevant())
			{
				if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(SubComponent))
				{
					PrimitiveComponents.Add(PrimitiveComponent);

					if (UInstancedStaticMeshComponent* ISMC = Cast<UInstancedStaticMeshComponent>(PrimitiveComponent))
					{
						UE_LOG(LogHLODBuilder, Verbose, TEXT("%s    * %s [%d instances]"), Padding, *ISMC->GetStaticMesh()->GetName(), ISMC->GetInstanceCount());
					}
					else if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(PrimitiveComponent))
					{
						UE_LOG(LogHLODBuilder, Verbose, TEXT("%s    * %s"), Padding, *SMC->GetStaticMesh()->GetName());
					}
				}
				else
				{
					UE_LOG(LogHLODBuilder, Warning, TEXT("Component \"%s\" is marked as HLOD-relevant but this type of component currently unsupported."), *SubComponent->GetFullName());
				}
			}
		}
	};

	TSet<AActor*> UnderlyingActors;

	for (const FWorldPartitionReference& ActorRef : InActors)
	{
		AActor* Actor = ActorRef->GetActor();

		// Gather primitives from the Actor
		GatherPrimitivesFromActor(Actor);

		// Retrieve all underlying actors (ex: all sub actors of a LevelInstance)
		UnderlyingActors.Reset();
		Actor->EditorGetUnderlyingActors(UnderlyingActors);

		// Gather primitives from underlying actors
		for (const AActor* UnderlyingActor : UnderlyingActors)
		{
			if (UnderlyingActor->IsHLODRelevant())
			{
				GatherPrimitivesFromActor(UnderlyingActor, Actor);
			}
		}
	}

	return PrimitiveComponents;
}

void FHLODBuilder::DisableCollisions(UPrimitiveComponent* Component)
{
	Component->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Component->SetGenerateOverlapEvents(false);
	Component->SetCanEverAffectNavigation(false);
	Component->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_No;
	Component->SetCanEverAffectNavigation(false);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
