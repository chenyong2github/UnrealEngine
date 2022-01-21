// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDeformerInstance.h"

#include "OptimusDeformer.h"

#include "Components/MeshComponent.h"


void UOptimusDeformerInstance::SetupFromDeformer(
	UOptimusDeformer* InDeformer
	)
{
	ComputeGraphExecInfos.Reset();
	
	UMeshComponent* MeshComponentPtr = MeshComponent.Get();
	if (!MeshComponentPtr)
	{
		return;
	}
	
	for (const FOptimusComputeGraphInfo& ComputeGraphInfo: InDeformer->ComputeGraphs)
	{
		FOptimusDeformerInstanceExecInfo Info;
		Info.ComputeGraph = ComputeGraphInfo.ComputeGraph;
		Info.ComputeGraphInstance.CreateDataProviders(Info.ComputeGraph, MeshComponentPtr);
		ComputeGraphExecInfos.Add(Info);
	}
	
	MeshComponentPtr->MarkRenderDynamicDataDirty();
}


bool UOptimusDeformerInstance::IsActive() const
{
	for (const FOptimusDeformerInstanceExecInfo& Info: ComputeGraphExecInfos)
	{
		if (!Info.ComputeGraphInstance.ValidateDataProviders(Info.ComputeGraph))
		{
			return false;
		}
	}
	return !ComputeGraphExecInfos.IsEmpty();
}


void UOptimusDeformerInstance::EnqueueWork(FSceneInterface* InScene, EWorkLoad WorkLoadType)
{
	for (FOptimusDeformerInstanceExecInfo& Info: ComputeGraphExecInfos)
	{
		Info.ComputeGraphInstance.EnqueueWork(Info.ComputeGraph, InScene);
	}
}
