// Copyright Epic Games, Inc. All Rights Reserved.

#include "HLODBuilderInstancing.h"

#include "Misc/HashBuilder.h"
#include "Engine/StaticMesh.h"
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


TArray<UPrimitiveComponent*> FHLODBuilder_Instancing::CreateComponents(AWorldPartitionHLOD* InHLODActor, const UHLODLayer* InHLODLayer, const TArray<UPrimitiveComponent*>& InSubComponents)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FHLODBuilder_Instancing::CreateComponents);

	TArray<UPrimitiveComponent*> Components;

	// Gather all meshes to instantiate along with their transforms
	TMap<FInstancingKey, TArray<UPrimitiveComponent*>> Instances;
	for (UPrimitiveComponent* Primitive : InSubComponents)
	{
		if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Primitive))
		{
			Instances.FindOrAdd(FInstancingKey(SMC)).Add(SMC);
		}
	}

	// Create an ISMC for each SM asset we found
	for (const auto& Entry : Instances)
	{
		const FInstancingKey EntryInstancingKey = Entry.Key;
		const TArray<UPrimitiveComponent*>& EntryComponents = Entry.Value;
			
		UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(InHLODActor);
		EntryInstancingKey.ApplyTo(Component);
		Component->SetForcedLodModel(Component->GetStaticMesh()->GetNumLODs());

		DisableCollisions(Component);

		// Add all instances
		for (UPrimitiveComponent* SMC : EntryComponents)
		{
			// If we have an ISMC, retrieve all instances
			if (UInstancedStaticMeshComponent* InstancedStaticMeshComponent = Cast<UInstancedStaticMeshComponent>(SMC))
			{
				for (int32 InstanceIdx = 0; InstanceIdx < InstancedStaticMeshComponent->GetInstanceCount(); InstanceIdx++)
				{
					FTransform InstanceTransform;
					InstancedStaticMeshComponent->GetInstanceTransform(InstanceIdx, InstanceTransform, true);
					Component->AddInstanceWorldSpace(InstanceTransform);
				}
			}
			else
			{
				Component->AddInstanceWorldSpace(SMC->GetComponentTransform());
			}
		}

		Components.Add(Component);
	};

	return Components;
}
