// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusMeshDeformer.h"
#include "Components/MeshComponent.h"

UMeshDeformerInstance* UOptimusMeshDeformer::CreateInstance(UMeshComponent* InMeshComponent)
{
	if (ComputeGraph == nullptr || InMeshComponent == nullptr)
	{
		return nullptr;
	}

	UOptimusMeshDeformerInstance* Instance = NewObject<UOptimusMeshDeformerInstance>();
	Instance->ComputeGraph = ComputeGraph;
	Instance->ComputeGraphInstance.CreateDataProviders(ComputeGraph, InMeshComponent);
	return Instance;
}

bool UOptimusMeshDeformerInstance::IsActive() const
{
	return ComputeGraphInstance.ValidateDataProviders(ComputeGraph);
}

void UOptimusMeshDeformerInstance::EnqueueWork(FSceneInterface* InScene, EWorkLoad WorkLoadType)
{
	ComputeGraphInstance.EnqueueWork(ComputeGraph, InScene);
}
