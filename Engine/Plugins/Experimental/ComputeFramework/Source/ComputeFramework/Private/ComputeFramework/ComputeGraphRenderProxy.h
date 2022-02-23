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
		FIntVector GroupDim;
		FComputeKernelResource const* KernelResource = nullptr;
		FShaderParametersMetadata const* ShaderMetadata = nullptr;
		FComputeKernelPermutationVector const* ShaderPermutationVector = nullptr;
		TArray<int32> BoundProviderIndices;
	};

	TArray<FKernelInvocation> KernelInvocations;
};
