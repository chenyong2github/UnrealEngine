// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FComputeKernelResource;
class FShaderParametersMetadata;
class UComputeGraph;

/** 
 * Render thread proxy object for a UComputeGraph. 
 * FComputeGraphRenderProxy objects are created every frame for each graph that is scheduled for execution.
 */
class FComputeGraphRenderProxy
{
public:
	/** Description for each kernel in the graph. */
	struct FKernelInvocation
	{
		FName KernelName;
		FIntVector KernelGroupSize = FIntVector(1, 1, 1);
		FComputeKernelResource const* KernelResource = nullptr;
		FShaderParametersMetadata const* ShaderMetadata = nullptr;
		FComputeKernelPermutationVector const* ShaderPermutationVector = nullptr;
		TArray<int32> BoundProviderIndices;
		int32 ExecutionProviderIndex = -1;
	};

	TArray<FKernelInvocation> KernelInvocations;
};
