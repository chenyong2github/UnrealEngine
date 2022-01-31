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


const TArray<UOptimusVariableDescription*>& UOptimusDeformerInstance::GetVariables() const
{
	static TArray<UOptimusVariableDescription*> Dummy;
	return Dummy;
}


bool UOptimusDeformerInstance::SetBoolVariable(FName InVariableName, bool InValue)
{
	return false;
}


bool UOptimusDeformerInstance::SetIntVariable(FName InVariableName, int32 InValue)
{
	return false;
}


bool UOptimusDeformerInstance::SetFloatVariable(FName InVariableName, float InValue)
{
	return false;
}


bool UOptimusDeformerInstance::SetVectorVariable(FName InVariableName, const FVector& InValue)
{
	return false;
}


bool UOptimusDeformerInstance::SetVector4Variable(FName InVariableName, const FVector4& InValue)
{
	return false;
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
