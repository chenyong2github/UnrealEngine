// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshSelectors/PCGMeshSelectorBase.h"

#include "Elements/PCGStaticMeshSpawner.h"

void UPCGMeshSelectorBase::SelectInstances_Implementation(
	FPCGContext& Context, 
	const UPCGStaticMeshSpawnerSettings* Settings, 
	const UPCGSpatialData* InSpatialData,
	TArray<FPCGMeshInstanceList>& OutMeshInstances,
	UPCGPointData* OutPointData) const
{
	PCGE_LOG_C(Error, &Context, "Invalid use of abstract MeshSelectorBase class. Please use an already existing class or implement the CreateMeshInstanceData method");
}

bool UPCGMeshSelectorBase::FindOrAddInstanceList(
	TArray<FPCGMeshInstanceList>& OutInstanceLists,
	const TSoftObjectPtr<UStaticMesh>& Mesh,
	bool bOverrideCollisionProfile,
	const FCollisionProfileName& CollisionProfile,
	bool bOverrideMaterials,
	const TArray<UMaterialInterface*>& MaterialOverrides,
	int32& OutIndex) const
{
	for (int Index = 0; Index < OutInstanceLists.Num(); ++Index)
	{
		if (OutInstanceLists[Index].Mesh != Mesh || OutInstanceLists[Index].bOverrideCollisionProfile != bOverrideCollisionProfile || OutInstanceLists[Index].bOverrideMaterials != bOverrideMaterials)
		{
			continue;
		}

		if (OutInstanceLists[Index].bOverrideCollisionProfile && !OutInstanceLists[Index].CollisionProfile.Name.IsEqual(CollisionProfile.Name))
		{
			continue;
		}

		if (OutInstanceLists[Index].bOverrideMaterials && OutInstanceLists[Index].MaterialOverrides != MaterialOverrides)
		{
			continue;
		}

		OutIndex = Index;

		return false;
	}

	OutIndex = OutInstanceLists.Emplace(Mesh, bOverrideCollisionProfile, CollisionProfile, bOverrideMaterials, MaterialOverrides);

	return true;
}
