// Copyright Epic Games, Inc. All Rights Reserved.

#include "HLODBuilderInstancing.h"

#include "Misc/HashBuilder.h"
#include "Engine/StaticMesh.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODLayer.h"


// We want to merge all SMC that are using the same static mesh
// However, we must also take material overiddes into account.
struct FInstancingKey
{
	FInstancingKey(const UStaticMeshComponent* SMC)
	{
		FHashBuilder HashBuilder;

		StaticMesh = SMC->GetStaticMesh();
		HashBuilder << StaticMesh;

		const int32 NumMaterials = SMC->GetNumMaterials();
		Materials.Reserve(NumMaterials);

		for (int32 MaterialIndex = 0; MaterialIndex < NumMaterials; ++MaterialIndex)
		{
			UMaterialInterface* Material = SMC->GetMaterial(MaterialIndex);

			Materials.Add(Material);
			HashBuilder << Material;
		}

		Hash = HashBuilder.GetHash();
	}

	void ApplyTo(UStaticMeshComponent* SMC) const
	{
		// Set static mesh
		SMC->SetStaticMesh(StaticMesh);
			
		// Set material overrides
		for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
		{
			SMC->SetMaterial(MaterialIndex, Materials[MaterialIndex]);
		}
	}

	friend uint32 GetTypeHash(const FInstancingKey& Key)
	{
		return Key.Hash;
	}

	bool operator==(const FInstancingKey& Other) const
	{
		return Hash == Other.Hash && StaticMesh == Other.StaticMesh && Materials == Other.Materials;
	}

private:
	UStaticMesh*				StaticMesh;
	TArray<UMaterialInterface*>	Materials;
	uint32						Hash;
};

// Data used to batch instances
struct FInstancingData
{
	int32							NumInstances;

	TArray<FTransform>				InstancesTransforms;

	int32							NumCustomDataFloats;
	TArray<float>					InstancesCustomData;
};


TArray<UPrimitiveComponent*> FHLODBuilder_Instancing::CreateComponents(AWorldPartitionHLOD* InHLODActor, const UHLODLayer* InHLODLayer, const TArray<UPrimitiveComponent*>& InSubComponents)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FHLODBuilder_Instancing::CreateComponents);

	TArray<UPrimitiveComponent*> Components;

	// Gather all meshes to instantiate along with their transforms
	TMap<FInstancingKey, FInstancingData> InstancesData;
	for (UPrimitiveComponent* Primitive : InSubComponents)
	{
		if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Primitive))
		{
			FInstancingData& InstancingData = InstancesData.FindOrAdd(FInstancingKey(SMC));

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
		const FInstancingKey& EntryInstancingKey = Entry.Key;
		FInstancingData& EntryInstancingData = Entry.Value;

		EntryInstancingData.InstancesTransforms.Reset(EntryInstancingData.NumInstances);
		EntryInstancingData.InstancesCustomData.Reset(EntryInstancingData.NumInstances * EntryInstancingData.NumCustomDataFloats);
	}
	
	// Append all transforms & per instance custom data
	for (UPrimitiveComponent* Primitive : InSubComponents)
	{
		if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Primitive))
		{
			FInstancingData& InstancingData = InstancesData.FindChecked(FInstancingKey(SMC));

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
		const FInstancingKey& EntryInstancingKey = Entry.Key;
		FInstancingData& EntryInstancingData = Entry.Value;

		UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(InHLODActor);
		EntryInstancingKey.ApplyTo(Component);
		Component->SetForcedLodModel(Component->GetStaticMesh()->GetNumLODs());

		DisableCollisions(Component);

		Component->NumCustomDataFloats = EntryInstancingData.NumCustomDataFloats;
		Component->AddInstances(EntryInstancingData.InstancesTransforms, /*bShouldReturnIndices*/false, /*bWorldSpace*/true);
		Component->PerInstanceSMCustomData = MoveTemp(EntryInstancingData.InstancesCustomData);

		Components.Add(Component);
	};

	return Components;
}
