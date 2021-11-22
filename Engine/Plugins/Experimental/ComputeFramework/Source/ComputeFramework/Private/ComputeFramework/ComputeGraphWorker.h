// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComputeFramework/ComputeDataProvider.h"
#include "ComputeWorkerInterface.h"
#include "RHIDefinitions.h"
#include "Shader.h"

class FComputeDataProviderRenderProxy;
class FComputeKernelResource;
class FComputeKernelShader;
class UComputeGraph;

/** 
 * Render thread proxy object for a UComputeGraph. 
 * FComputeGraphProxy objects are created every frame for each graph that is scheduled for execution.
 */
class FComputeGraphProxy
{
public:
	/** Called on the game thread to set up the data required by the render thread. */
	void Initialize(UComputeGraph* ComputeGraph);

	/** Description for each kernel in the graph. */
	struct FKernelInvocation
	{
		FName KernelName;
		FName InvocationName;
		FIntVector GroupDim;
		FShaderParametersMetadata const* ShaderMetadata = nullptr;
		TMap<int32, TArray<uint8>> ShaderParamBindings;
		FComputeKernelResource const* Kernel = nullptr;
	};

	TArray<FKernelInvocation> KernelInvocations;
};

/** 
 * Class that manages the scheduling of Compute Graph work.
 * Work can be enqueued on the render thread for the execution at the next call to ExecuteBatches().
 */
class FComputeGraphTaskWorker : public IComputeTaskWorker
{
public:
	/** Enqueue a compute graph for execution. */
	void Enqueue(
		const FComputeGraphProxy* ComputeGraph,
		TArray<FComputeDataProviderRenderProxy*> ComputeDataProviders );

	/** Submit enqueued compute graph work. */
	void SubmitWork(
		FRHICommandListImmediate& RHICmdList,
		ERHIFeatureLevel::Type FeatureLevel ) override;

private:
	/** Description of each dispatch that is enqueued. */
	struct FShaderInvocation
	{
		FName KernelName;
		FName InvocationName;
		FIntVector DispatchDim;
		const FShaderParametersMetadata* ShaderParamMetadata = nullptr;
		TMap<int32, TArray<uint8>> ShaderParamBindings;
		TShaderRef<FComputeKernelShader> Shader;
		int32 SubInvocationIndex = 0;
	};

	/**
	 * Description of each graph that is enqueued.
	 * todo[CF]: We probably need more context for dispatching work with minimal overhead. For example we would like to overlap UAVs on any skin cache writing.
	 */
	struct FGraphInvocation
	{
		/** Shader invocations to dispatch. */
		TArray<FShaderInvocation> ComputeShaders;
		TArray<FComputeDataProviderRenderProxy*> DataProviders;
		int32 NumSubInvocations;
	
		~FGraphInvocation();
	};

	TArray<FGraphInvocation> GraphInvocations;
};
