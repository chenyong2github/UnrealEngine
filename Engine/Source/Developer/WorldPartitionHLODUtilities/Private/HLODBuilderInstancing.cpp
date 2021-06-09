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
			
			// For now, ignore ray tracing group ID when batching
			// We may want to expose an instance batching option to control this
			RayTracingGroupId = FPrimitiveSceneProxy::InvalidRayTracingGroupId;

			ComputeHash();
		}
	};

	// Store batched instances data
	struct FInstancingData
	{
		int32							NumInstances;

		TArray<FTransform>				InstancesTransforms;

		int32							NumCustomDataFloats;
		TArray<float>					InstancesCustomData;
	};
}

TArray<UPrimitiveComponent*> FHLODBuilder_Instancing::CreateComponents(AWorldPartitionHLOD* InHLODActor, const UHLODLayer* InHLODLayer, const TArray<UPrimitiveComponent*>& InSubComponents)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FHLODBuilder_Instancing::CreateComponents);

	TArray<UPrimitiveComponent*> Components;

	// Prepare instance batches
	TMap<FISMComponentDescriptor, FInstancingData> InstancesData;
	for (UPrimitiveComponent* Primitive : InSubComponents)
	{
		if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Primitive))
		{
			FCustomISMComponentDescriptor ISMComponentDescriptor(SMC);
			FInstancingData& InstancingData = InstancesData.FindOrAdd(ISMComponentDescriptor);

			if (UInstancedStaticMeshComponent* ISMC = Cast<UInstancedStaticMeshComponent>(SMC))
			{
				InstancingData.NumCustomDataFloats = FMath::Max(InstancingData.NumCustomDataFloats, ISMC->PerInstanceSMCustomData.Num());
				InstancingData.NumInstances += ISMC->GetInstanceCount();
			}
			else
			{
				InstancingData.NumInstances++;
			}
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
	for (UPrimitiveComponent* Primitive : InSubComponents)
	{
		if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Primitive))
		{
			FCustomISMComponentDescriptor ISMComponentDescriptor(SMC);
			FInstancingData& InstancingData = InstancesData.FindChecked(ISMComponentDescriptor);

			int32 NumCustomDataFloatsAdded = 0;

			if (UInstancedStaticMeshComponent* ISMC = Cast<UInstancedStaticMeshComponent>(SMC))
			{
				// Add transforms
				for (int32 InstanceIdx = 0; InstanceIdx < ISMC->GetInstanceCount(); InstanceIdx++)
				{
					FTransform& InstanceTransform = InstancingData.InstancesTransforms.AddDefaulted_GetRef();
					ISMC->GetInstanceTransform(InstanceIdx, InstanceTransform, true);
				}

				// Add per instance custom data
				InstancingData.InstancesCustomData.Append(ISMC->PerInstanceSMCustomData);
				NumCustomDataFloatsAdded = ISMC->PerInstanceSMCustomData.Num();
			}
			else
			{
				InstancingData.InstancesTransforms.Add(SMC->GetComponentTransform());
			}

			// Add missing custom data, if any
			InstancingData.InstancesCustomData.AddDefaulted(InstancingData.NumCustomDataFloats - NumCustomDataFloatsAdded);
		}
	}

	// Create an ISMC for each SM asset we found
	for (auto& Entry : InstancesData)
	{
		const FISMComponentDescriptor& ISMComponentDescriptor = Entry.Key;
		FInstancingData& EntryInstancingData = Entry.Value;

		UInstancedStaticMeshComponent* Component = ISMComponentDescriptor.CreateComponent(InHLODActor);
		Component->SetForcedLodModel(Component->GetStaticMesh()->GetNumLODs());

		DisableCollisions(Component);

		Component->NumCustomDataFloats = EntryInstancingData.NumCustomDataFloats;
		Component->AddInstances(EntryInstancingData.InstancesTransforms, /*bShouldReturnIndices*/false, /*bWorldSpace*/true);
		Component->PerInstanceSMCustomData = MoveTemp(EntryInstancingData.InstancesCustomData);

		Components.Add(Component);
	};

	return Components;
}
