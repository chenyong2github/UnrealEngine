// Copyright Epic Games, Inc. All Rights Reserved.

#include "HLOD/HLODProxyDesc.h"

#if WITH_EDITOR
#include "Engine/LODActor.h"
#include "Algo/Transform.h"
#include "GameFramework/WorldSettings.h"
#endif

#if WITH_EDITOR

FHLODISMComponentDesc::FHLODISMComponentDesc(const UInstancedStaticMeshComponent* InISMComponent, const UMaterialInterface* InMaterial)
{
	Material = InMaterial;
	StaticMesh = InISMComponent->GetStaticMesh();

	Instances.Reset(InISMComponent->GetInstanceCount());
	for (const FInstancedStaticMeshInstanceData& InstanceData : InISMComponent->PerInstanceSMData)
	{
		Instances.Emplace(InstanceData.Transform);
	}
}

bool UHLODProxyDesc::UpdateFromLODActor(const ALODActor* InLODActor)
{
	// Check if there's any difference between the LODActor & its description
	if (!ShouldUpdateDesc(InLODActor))
	{
		return false;
	}

	// A difference was detected, copy all parameters from the LODActor
	
	Modify();

	SubActors.Reset(InLODActor->SubActors.Num());
	SubHLODDescs.Reset();
	
	for (AActor* SubActor : InLODActor->SubActors)
	{
		if (ALODActor* SubLODActor = Cast<ALODActor>(SubActor))
		{
			check(SubLODActor->ProxyDesc);
			SubHLODDescs.Emplace(SubLODActor->ProxyDesc);
		}
		else
		{
			SubActors.Emplace(SubActor->GetFName());
		}
	}

	StaticMesh = InLODActor->StaticMeshComponent->GetStaticMesh();

	const TMap<const UMaterialInterface*, UInstancedStaticMeshComponent*>& ISMComponents = InLODActor->ImpostersStaticMeshComponents;
	ISMComponentsDesc.Reset(ISMComponents.Num());
	for (auto const& Pair : ISMComponents)
	{
		ISMComponentsDesc.Emplace(Pair.Value, Pair.Key);
	}

	LODDrawDistance = InLODActor->GetDrawDistance();
	bOverrideMaterialMergeSettings = InLODActor->bOverrideMaterialMergeSettings;
	MaterialSettings = InLODActor->MaterialSettings;
	bOverrideTransitionScreenSize = InLODActor->bOverrideTransitionScreenSize;
	TransitionScreenSize = InLODActor->TransitionScreenSize;
	bOverrideScreenSize = InLODActor->bOverrideScreenSize;
	ScreenSize = InLODActor->ScreenSize;

	Key = InLODActor->Key;
	LODLevel = InLODActor->LODLevel;
	LODActorTag = InLODActor->LODActorTag;

	return true;
}

bool UHLODProxyDesc::ShouldUpdateDesc(const ALODActor* InLODActor) const
{
	TArray<FName> LocalSubActors;
	TArray<TSoftObjectPtr<UHLODProxyDesc>> LocalSubHLODDescs;

	LocalSubActors.Reset(InLODActor->SubActors.Num());
	for (AActor* SubActor : InLODActor->SubActors)
	{
		if (ALODActor* SubLODActor = Cast<ALODActor>(SubActor))
		{
			check(SubLODActor->ProxyDesc);
			LocalSubHLODDescs.Emplace(SubLODActor->ProxyDesc);
		}
		else
		{
			LocalSubActors.Emplace(SubActor->GetFName());
		}
	}

	if (LocalSubActors != SubActors)
	{
		return true;
	}

	if (LocalSubHLODDescs != SubHLODDescs)
	{
		return true;
	}

	if (StaticMesh != InLODActor->StaticMeshComponent->GetStaticMesh())
	{
		return true;
	}

	TArray<FHLODISMComponentDesc> LocalISMComponentsDesc;
	const TMap<const UMaterialInterface*, UInstancedStaticMeshComponent*>& ISMComponents = InLODActor->ImpostersStaticMeshComponents;
	LocalISMComponentsDesc.Reset(ISMComponents.Num());
	for (auto const& Pair : ISMComponents)
	{
		LocalISMComponentsDesc.Emplace(Pair.Value, Pair.Key);
	}

	if (LocalISMComponentsDesc != ISMComponentsDesc)
	{
		return true;
	}

	if (LODDrawDistance != InLODActor->GetDrawDistance())
	{
		return true;
	}

	if (bOverrideMaterialMergeSettings != InLODActor->bOverrideMaterialMergeSettings)
	{
		return true;
	}

	if (MaterialSettings != InLODActor->MaterialSettings)
	{
		return true;
	}

	if (bOverrideTransitionScreenSize != InLODActor->bOverrideTransitionScreenSize)
	{
		return true;
	}

	if (TransitionScreenSize != InLODActor->TransitionScreenSize)
	{
		return true;
	}
	
	if (bOverrideScreenSize != InLODActor->bOverrideScreenSize)
	{
		return true;
	}
	
	if (ScreenSize != InLODActor->ScreenSize)
	{
		return true;
	}

	if (Key != InLODActor->Key)
	{
		return true;
	}

	if (LODLevel != InLODActor->LODLevel)
	{
		return true;
	}

	if (LODActorTag != InLODActor->LODActorTag)
	{
		return true;
	}

	return false;
}

ALODActor* UHLODProxyDesc::SpawnLODActor(ULevel* InLevel) const
{
	FActorSpawnParameters ActorSpawnParameters;
	ActorSpawnParameters.Name = MakeUniqueObjectName(InLevel, ALODActor::StaticClass());
	ActorSpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ActorSpawnParameters.OverrideLevel = InLevel;
	ActorSpawnParameters.bHideFromSceneOutliner = true;

	ALODActor* LODActor = InLevel->GetWorld()->SpawnActor<ALODActor>(ALODActor::StaticClass(), ActorSpawnParameters);

	// Temporarily switch to movable in order to update the static mesh...
	LODActor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	LODActor->SetStaticMesh(StaticMesh);
	LODActor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);

	for (const FHLODISMComponentDesc& ISMComponentDesc : ISMComponentsDesc)
	{
		LODActor->SetupImposters(ISMComponentDesc.Material, ISMComponentDesc.StaticMesh, ISMComponentDesc.Instances);
	}

	LODActor->SetDrawDistance(LODDrawDistance);
	LODActor->bOverrideMaterialMergeSettings = bOverrideMaterialMergeSettings;
	LODActor->MaterialSettings = MaterialSettings;
	LODActor->bOverrideTransitionScreenSize = bOverrideTransitionScreenSize;
	LODActor->TransitionScreenSize = TransitionScreenSize;
	LODActor->bOverrideScreenSize = bOverrideScreenSize;
	LODActor->ScreenSize = ScreenSize;
	LODActor->Key = Key;
	LODActor->LODLevel = LODLevel;
	LODActor->LODActorTag = LODActorTag;
	
	LODActor->CachedNumHLODLevels = InLevel->GetWorldSettings()->GetNumHierarchicalLODLevels();

	TArray<AActor*> SubActorsToAdd;
	SubActorsToAdd.Reset(SubActors.Num());

	// Add sub LODActors spawned from SubActorsDescs
	for (AActor* Actor : InLevel->Actors)
	{
		if (ALODActor* SubLODActor = Cast<ALODActor>(Actor))
		{
			if (SubHLODDescs.Contains(SubLODActor->ProxyDesc))
			{
				SubActorsToAdd.Add(SubLODActor);
			}
		}
	}

	// Find all subactors from the level
	Algo::Transform(SubActors, SubActorsToAdd, [InLevel](const FName& ActorName)
	{
		return FindObjectFast<AActor>(InLevel, ActorName);
	});

	// Remove null entries
	SubActorsToAdd.RemoveAll([](AActor* Actor) { return Actor == nullptr; });

	LODActor->AddSubActors(SubActorsToAdd);

	LODActor->ProxyDesc = const_cast<UHLODProxyDesc*>(this);
	LODActor->bBuiltFromHLODDesc = true;

	return LODActor;
}

#endif // #if WITH_EDITOR
