// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeGraph.h"
#include "ComputeFramework/ComputeKernel.h"
#include "UObject/UObjectGlobals.h"


FComputeKernelInvocationHandle UComputeGraph::AddKernel(UComputeKernelSource* KernelSource)
{
	return {};
}

bool UComputeGraph::RemoveKernel(FComputeKernelInvocationHandle KernelInvocation)
{
	return false;
}
