// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeGraph.h"
#include "ComputeFramework/ComputeKernel.h"
#include "UObject/UObjectGlobals.h"


// #TODO_ZABIR: TEMP
#include "ComputeFramework/ComputeKernelFromText.h"


FComputeKernelInvocationHandle UComputeGraph::AddKernel(UComputeKernelSource* KernelSource)
{
	return {};
}

bool UComputeGraph::RemoveKernel(FComputeKernelInvocationHandle KernelInvocation)
{
	return false;
}

void UComputeGraph::PostLoad()
{
	Super::PostLoad();

	UComputeKernel* Kernel = LoadObject<UComputeKernel>(
		nullptr, 
		TEXT("/Game/Developers/ZabirHoque/CF/LBS_Kernel.LBS_Kernel"),
		nullptr,
		LOAD_None, 
		nullptr
		);

	KernelInvocations.Reset();
	KernelInvocations.Emplace(Kernel, 1);
}
