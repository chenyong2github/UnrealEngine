// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshSelectors/PCGMeshSelectorBase.h"

#include "Elements/PCGStaticMeshSpawner.h"

void UPCGMeshSelectorBase::SelectInstances_Implementation(
	FPCGContext& Context, 
	const UPCGStaticMeshSpawnerSettings* Settings, 
	const UPCGSpatialData* InSpatialData,
	TMap<TSoftObjectPtr<UStaticMesh>, FPCGMeshInstanceList>& OutMeshInstances) const
{
	PCGE_LOG_C(Error, &Context, "Invalid use of abstract MeshSelectorBase class. Please use an already existing class or implement the CreateMeshInstanceData method");
}
