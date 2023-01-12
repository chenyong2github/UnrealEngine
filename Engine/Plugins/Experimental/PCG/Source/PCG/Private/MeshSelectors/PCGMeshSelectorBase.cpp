// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshSelectors/PCGMeshSelectorBase.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGMeshSelectorBase)

int32 UPCGMeshSelectorBase::FindOrAddInstanceList(
	TArray<FPCGMeshInstanceList>& OutInstanceLists,
	const TSoftObjectPtr<UStaticMesh>& Mesh,
	bool bOverrideCollisionProfile,
	const FCollisionProfileName& CollisionProfile,
	bool bOverrideMaterials,
	const TArray<UMaterialInterface*>& MaterialOverrides,
	const float InCullStartDistance,
	const float InCullEndDistance) const
{
	for (int Index = 0; Index < OutInstanceLists.Num(); ++Index)
	{
		if (OutInstanceLists[Index].Mesh != Mesh || OutInstanceLists[Index].bOverrideCollisionProfile != bOverrideCollisionProfile || OutInstanceLists[Index].bOverrideMaterials != bOverrideMaterials)
		{
			continue;
		}

		if (OutInstanceLists[Index].CullStartDistance != InCullStartDistance || OutInstanceLists[Index].CullEndDistance != InCullEndDistance)
		{
			continue;
		}

		if (OutInstanceLists[Index].bOverrideCollisionProfile && !OutInstanceLists[Index].CollisionProfile.Name.IsEqual(CollisionProfile.Name))
		{
			continue;
		}

		if (OutInstanceLists[Index].bOverrideMaterials && ToRawPtrTArrayUnsafe(OutInstanceLists[Index].MaterialOverrides) != MaterialOverrides)
		{
			continue;
		}

		return Index;
	}

	return OutInstanceLists.Emplace(Mesh, bOverrideCollisionProfile, CollisionProfile, bOverrideMaterials, MaterialOverrides, InCullStartDistance, InCullEndDistance);
}

