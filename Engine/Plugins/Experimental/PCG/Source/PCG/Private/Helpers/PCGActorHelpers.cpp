// Copyright Epic Games, Inc. All Rights Reserved.

#include "Helpers/PCGActorHelpers.h"

#include "PCGHelpers.h"

#include "PCGComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"

UInstancedStaticMeshComponent* UPCGActorHelpers::GetOrCreateISMC(AActor* InTargetActor, const UPCGComponent* InSourceComponent, UStaticMesh* InMesh, const TArray<UMaterialInterface*>& InMaterials)
{
	check(InTargetActor != nullptr && InMesh != nullptr);

	TArray<UInstancedStaticMeshComponent*> ISMCs;
	InTargetActor->GetComponents<UInstancedStaticMeshComponent>(ISMCs);

	for (UInstancedStaticMeshComponent* ISMC : ISMCs)
	{
		if (ISMC->GetStaticMesh() == InMesh &&
			(!InSourceComponent || ISMC->ComponentTags.Contains(InSourceComponent->GetFName())))
		{
			// If materials are provided, we'll make sure they match to the already set materials.
			// If not provided, we'll make sure that the current materials aren't overriden
			bool bMaterialsMatched = true;

			for (int32 MaterialIndex = 0; MaterialIndex < ISMC->GetNumMaterials() && bMaterialsMatched; ++MaterialIndex)
			{
				if (MaterialIndex < InMaterials.Num() && InMaterials[MaterialIndex])
				{
					if (InMaterials[MaterialIndex] != ISMC->GetMaterial(MaterialIndex))
					{
						bMaterialsMatched = false;
					}
				}
				else
				{
					// Material is currently overriden
					if (ISMC->OverrideMaterials.IsValidIndex(MaterialIndex) && ISMC->OverrideMaterials[MaterialIndex])
					{
						bMaterialsMatched = false;
					}
				}
			}

			if (bMaterialsMatched)
			{
				return ISMC;
			}
		}
	}

	InTargetActor->Modify();

	// Otherwise, create a new component
	// TODO: use static mesh component if there's only one instance
	// TODO: add hism/ism switch or better yet, use a template component
	UInstancedStaticMeshComponent* ISMC = NewObject<UHierarchicalInstancedStaticMeshComponent>(InTargetActor);
	ISMC->SetStaticMesh(InMesh);

	// TODO: improve material override mechanisms
	const int32 NumMaterials = ISMC->GetNumMaterials();
	for (int32 MaterialIndex = 0; MaterialIndex < NumMaterials; ++MaterialIndex)
	{
		ISMC->SetMaterial(MaterialIndex, MaterialIndex < InMaterials.Num() ? InMaterials[MaterialIndex] : nullptr);
	}

	ISMC->RegisterComponent();
	InTargetActor->AddInstanceComponent(ISMC);
	ISMC->SetMobility(EComponentMobility::Static);
	// TODO: add option for collision, or use a template
	ISMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ISMC->AttachToComponent(InTargetActor->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);

	if (InSourceComponent)
	{
		ISMC->ComponentTags.Add(InSourceComponent->GetFName());
	}
	
	ISMC->ComponentTags.Add(PCGHelpers::DefaultPCGTag);

	return ISMC;
}