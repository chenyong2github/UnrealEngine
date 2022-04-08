// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComputeFramework/ComputeDataProvider.h"
#include "ComputeWorkerInterface.h"
#include "RHIDefinitions.h"
#include "Shader.h"

class FComputeDataProviderRenderProxy;
class FComputeGraphRenderProxy;
class FComputeKernelResource;

/** 
 * Class that manages the scheduling of Compute Graph work.
 * Work can be enqueued on the render thread for the execution at the next call to ExecuteBatches().
 */
class FComputeGraphTaskWorker : public IComputeTaskWorker
{
public:
	/** Enqueue a compute graph for execution. */
	void Enqueue(
		FComputeGraphRenderProxy const* ComputeGraph,
		TArray<FComputeDataProviderRenderProxy*> ComputeDataProviders );

	/** Submit enqueued compute graph work. */
	void SubmitWork(
		FRHICommandListImmediate& RHICmdList,
		ERHIFeatureLevel::Type FeatureLevel ) override;

private:
	/** Description of each kernel that is enqueued. */
	struct FShaderInvocation
	{
		FString KernelName;
		FIntVector KernelGroupSize = FIntVector(1, 1, 1);
		FComputeKernelResource const* KernelResource = nullptr;
		FShaderParametersMetadata const* ShaderParamMetadata = nullptr;
		FComputeKernelPermutationVector const* ShaderPermutationVector = nullptr;
		int32 ExecutionProviderIndex = -1;
	};

	/**
	 * Description of each graph that is enqueued.
	 * todo[CF]: We probably need more context for dispatching work with minimal overhead. For example we would like to overlap UAVs on any skin cache writing.
	 */
	struct FGraphInvocation
	{
		/** Graph name for profiling/debugging. */
		FName OwnerName;
		FName GraphName;
		/** Shader invocations to dispatch. */
		TArray<FShaderInvocation> ComputeShaders;
		/** Data provider proxies. */
		TArray<FComputeDataProviderRenderProxy*> DataProviderProxies;
		
		~FGraphInvocation();
	};

	TArray<FGraphInvocation> GraphInvocations;
};
