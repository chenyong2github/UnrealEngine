// Copyright Epic Games, Inc. All Rights Reserved.

#include "HLODBuilderInstancing.h"

#include "Misc/HashBuilder.h"
#include "Engine/StaticMesh.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODLayer.h"

#include "ISMPartition/ISMComponentDescriptor.h"

namespace 
{
	// Instance batcher class based on FISMComponentDescriptor
	struct FCustomISMComponentDescriptor : public FISMComponentDescriptor
	{
		FCustomISMComponentDescriptor(UStaticMeshComponent* SMC)
		{
			InitFrom(SMC, false);

			// We'll always want to spawn ISMC, even if our source components are all SMC
			ComponentClass = UInstancedStaticMeshComponent::StaticClass();

			ComputeHash();
		}
	};

	// Store batched instances data
	struct FInstancingData
	{
		int32							NumInstances = 0;

		TArray<FTransform>				InstancesTransforms;

		int32							NumCustomDataFloats = 0;
		TArray<float>					InstancesCustomData;

		TArray<FInstancedStaticMeshRandomSeed> RandomSeeds;
	};
}

UHLODBuilderInstancing::UHLODBuilderInstancing(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

TArray<UActorComponent*> UHLODBuilderInstancing::Build(const FHLODBuildContext& InHLODBuildContext, const TArray<UActorComponent*>& InSourceComponents) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UHLODBuilderInstancing::Build);

	TArray<UStaticMeshComponent*> SourceStaticMeshComponents = FilterComponents<UStaticMeshComponent>(InSourceComponents);
	
	// Prepare instance batches
	TMap<FISMComponentDescriptor, FInstancingData> InstancesData;
	for (UStaticMeshComponent* SMC : SourceStaticMeshComponents)
	{
		FCustomISMComponentDescriptor ISMComponentDescriptor(SMC);
		FInstancingData& InstancingData = InstancesData.FindOrAdd(ISMComponentDescriptor);

		if (UInstancedStaticMeshComponent* ISMC = Cast<UInstancedStaticMeshComponent>(SMC))
		{
			InstancingData.NumCustomDataFloats = FMath::Max(InstancingData.NumCustomDataFloats, ISMC->NumCustomDataFloats);
			InstancingData.RandomSeeds.Add( { InstancingData.NumInstances, ISMC->InstancingRandomSeed } );
			InstancingData.NumInstances += ISMC->GetInstanceCount();
		}
		else
		{
			InstancingData.NumInstances++;
		}
	}

	// Resize arrays
	for (auto& Entry : InstancesData)
	{
		const FISMComponentDescriptor& ISMComponentDescriptor = Entry.Key;
		FInstancingData& EntryInstancingData = Entry.Value;

		EntryInstancingData.InstancesTransforms.Reset(EntryInstancingData.NumInstances);
		EntryInstancingData.InstancesCustomData.Reset(EntryInstancingData.NumInstances * EntryInstancingData.NumCustomDataFloats);
	}
	
	// Append all transforms & per instance custom data
	for (UStaticMeshComponent* SMC : SourceStaticMeshComponents)
	{
		FCustomISMComponentDescriptor ISMComponentDescriptor(SMC);
		FInstancingData& InstancingData = InstancesData.FindChecked(ISMComponentDescriptor);

		if (UInstancedStaticMeshComponent* ISMC = Cast<UInstancedStaticMeshComponent>(SMC))
		{
			// Add transforms
			for (int32 InstanceIdx = 0; InstanceIdx < ISMC->GetInstanceCount(); InstanceIdx++)
			{
				FTransform& InstanceTransform = InstancingData.InstancesTransforms.AddDefaulted_GetRef();
				ISMC->GetInstanceTransform(InstanceIdx, InstanceTransform, true);
			}

			// Add per instance custom data
			int32 NumCustomDataFloatToAdd = ISMC->GetInstanceCount() * InstancingData.NumCustomDataFloats;
			InstancingData.InstancesCustomData.Append(ISMC->PerInstanceSMCustomData);
			InstancingData.InstancesCustomData.AddDefaulted(NumCustomDataFloatToAdd - ISMC->PerInstanceSMCustomData.Num());
		}
		else
		{
			InstancingData.InstancesTransforms.Add(SMC->GetComponentTransform());
			InstancingData.InstancesCustomData.AddDefaulted(InstancingData.NumCustomDataFloats);
		}
	}

	// Create an ISMC for each SM asset we found
	TArray<UActorComponent*> HLODComponents;
	for (auto& Entry : InstancesData)
	{
		const FISMComponentDescriptor& ISMComponentDescriptor = Entry.Key;
		FInstancingData& EntryInstancingData = Entry.Value;

		check(EntryInstancingData.InstancesTransforms.Num() * EntryInstancingData.NumCustomDataFloats == EntryInstancingData.InstancesCustomData.Num());

		UInstancedStaticMeshComponent* Component = ISMComponentDescriptor.CreateComponent(GetTransientPackage());
		Component->SetForcedLodModel(Component->GetStaticMesh()->GetNumLODs());
		Component->NumCustomDataFloats = EntryInstancingData.NumCustomDataFloats;
		Component->AddInstances(EntryInstancingData.InstancesTransforms, /*bShouldReturnIndices*/false, /*bWorldSpace*/true);
		Component->PerInstanceSMCustomData = MoveTemp(EntryInstancingData.InstancesCustomData);

		if (!EntryInstancingData.RandomSeeds.IsEmpty())
		{
			Component->InstancingRandomSeed = EntryInstancingData.RandomSeeds[0].RandomSeed;
		}

		if (EntryInstancingData.RandomSeeds.Num() > 1)
		{
			Component->AdditionalRandomSeeds = TArrayView<FInstancedStaticMeshRandomSeed>(&EntryInstancingData.RandomSeeds[1], EntryInstancingData.RandomSeeds.Num() - 1);
		}

		HLODComponents.Add(Component);
	};

	return HLODComponents;
}
