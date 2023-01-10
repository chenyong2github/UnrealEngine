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
		FName InExecutionGroupName,
		FName InOwnerName,
		FComputeGraphRenderProxy const* InGraphRenderProxy, 
		TArray<FComputeDataProviderRenderProxy*> InDataProviderRenderProxies,
		FSimpleDelegate InFallbackDelegate);

	/** Submit enqueued compute graph work. */
	void SubmitWork(
		FRDGBuilder& GraphBuilder,
		FName InExecutionGroupName,
		ERHIFeatureLevel::Type InFeatureLevel ) override;

private:
	/** Description of each graph that is enqueued. */
	struct FGraphInvocation
	{
		/** Name of owner object that invoked the graph. */
		FName OwnerName;
		/** Graph render proxy. */
		FComputeGraphRenderProxy const* GraphRenderProxy = nullptr;
		/** Data provider render proxies. */
		TArray<FComputeDataProviderRenderProxy*> DataProviderRenderProxies;
		/** Render thread fallback logic for invocations that are invalid. */
		FSimpleDelegate FallbackDelegate;
		
		~FGraphInvocation();
	};

	/** Map of enqueued work per execution group . */
	TMap<FName, TArray<FGraphInvocation> > GraphInvocationsPerGroup;
};
