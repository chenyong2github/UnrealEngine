// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusTestGraph.h"

#include "ComputeFramework/ComputeKernel.h"
#include "ComputeFramework/ComputeKernelSource.h"
#include "DataInterfaces/DataInterfaceSkeletalMeshRead.h"
#include "DataInterfaces/DataInterfaceSkinCacheWrite.h"

void UOptimusTestGraph::PostLoad()
{
	BuildTestGraph();
	Super::PostLoad();
}

#if WITH_EDITOR

void UOptimusTestGraph::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	BuildTestGraph();
	CacheResourceShadersForRendering();
}

#endif // WITH_EDITOR

void UOptimusTestGraph::BuildTestGraph()
{
	KernelInvocations.Reset();
	DataInterfaces.Reset();
	GraphEdges.Reset();
	
	if (Kernel == nullptr)
	{
		return;
	}
	
	KernelInvocations.Add(Kernel);

	// Hard code data interfaces.
	USkeletalMeshReadDataInterface* SkinnedMeshDataInterface = NewObject<USkeletalMeshReadDataInterface>();
	DataInterfaces.Add(SkinnedMeshDataInterface);
	USkeletalMeshSkinCacheDataInterface* SkinnedMeshSkinCacheInterface = NewObject<USkeletalMeshSkinCacheDataInterface>();
	DataInterfaces.Add(SkinnedMeshSkinCacheInterface);

	// Setup the graph edges using function name/type matching.
	// Generally function names don't need to match, but we make the assumption here to get something working without a graph editor.
	TArray<FShaderFunctionDefinition> const& Inputs = Kernel->KernelSource->ExternalInputs;
	for (int32 KernelBindingIndex = 0; KernelBindingIndex < Inputs.Num(); ++KernelBindingIndex)
	{
		FComputeGraphEdge GraphEdge;
		GraphEdge.bKernelInput = true;
		GraphEdge.KernelIndex = 0;
		GraphEdge.KernelBindingIndex = KernelBindingIndex;

		bool bFound = false;
		for (int32 DataInterfaceIndex = 0; DataInterfaceIndex < DataInterfaces.Num() && !bFound; ++DataInterfaceIndex)
		{
			TArray<FShaderFunctionDefinition> DataProviderFunctions;
			DataInterfaces[DataInterfaceIndex]->GetSupportedInputs(DataProviderFunctions);
			for (int32 DataInterfaceBindingIndex = 0; DataInterfaceBindingIndex < DataProviderFunctions.Num() && !bFound; ++DataInterfaceBindingIndex)
			{
				if (DataProviderFunctions[DataInterfaceBindingIndex].Name == Inputs[KernelBindingIndex].Name)
				{
					GraphEdge.DataInterfaceIndex = DataInterfaceIndex;
					GraphEdge.DataInterfaceBindingIndex = DataInterfaceBindingIndex;
					bFound = true;
				}
			}
		}

		if (bFound)
		{
			GraphEdges.Add(GraphEdge);
		}
	}

	TArray<FShaderFunctionDefinition> const& Outputs = Kernel->KernelSource->ExternalOutputs;
	for (int32 KernelBindingIndex = 0; KernelBindingIndex < Outputs.Num(); ++KernelBindingIndex)
	{
		FComputeGraphEdge GraphEdge;
		GraphEdge.bKernelInput = false;
		GraphEdge.KernelIndex = 0;
		GraphEdge.KernelBindingIndex = KernelBindingIndex;

		bool bFound = false;
		for (int32 DataInterfaceIndex = 0; DataInterfaceIndex < DataInterfaces.Num() && !bFound; ++DataInterfaceIndex)
		{
			TArray<FShaderFunctionDefinition> DataProviderFunctions;
			DataInterfaces[DataInterfaceIndex]->GetSupportedOutputs(DataProviderFunctions);
			for (int32 DataInterfaceBindingIndex = 0; DataInterfaceBindingIndex < DataProviderFunctions.Num() && !bFound; ++DataInterfaceBindingIndex)
			{
				if (DataProviderFunctions[DataInterfaceBindingIndex].Name == Outputs[KernelBindingIndex].Name)
				{
					GraphEdge.DataInterfaceIndex = DataInterfaceIndex;
					GraphEdge.DataInterfaceBindingIndex = DataInterfaceBindingIndex;
					bFound = true;
				}
			}
		}

		if (bFound)
		{
			GraphEdges.Add(GraphEdge);
		}
	}
}
