// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeGraphInstance.h"

#include "ComputeFramework/ComputeDataInterface.h"
#include "ComputeFramework/ComputeDataProvider.h"
#include "ComputeFramework/ComputeFrameworkModule.h"
#include "ComputeFramework/ComputeGraph.h"
#include "ComputeFramework/ComputeGraphWorker.h"
#include "ComputeFramework/ComputeSystem.h"

void FComputeGraphInstance::CreateDataProviders(UComputeGraph* InComputeGraph, UObject* InBindingObject)
{
	DataProviders.Reset();
	if (InComputeGraph != nullptr)
	{
		InComputeGraph->CreateDataProviders(InBindingObject, DataProviders);
	}
}

void FComputeGraphInstance::DestroyDataProviders()
{
	DataProviders.Reset();
}

bool FComputeGraphInstance::ValidateDataProviders(UComputeGraph* InComputeGraph) const
{
	return InComputeGraph != nullptr && InComputeGraph->IsCompiled() && InComputeGraph->ValidateProviders(DataProviders);
}

bool FComputeGraphInstance::EnqueueWork(UComputeGraph* InComputeGraph, FSceneInterface const* InScene)
{
	if (InComputeGraph == nullptr || InScene == nullptr)
	{
		// todo[CF]: We should have a default fallback for all cases where we can't submit work.
		return false;
	}

	if (FComputeFrameworkModule::GetComputeSystem() == nullptr)
	{
		return false;
	}

	// Lookup the compute worker associated with this scene.
	FComputeGraphTaskWorker* ComputeGraphWorker = FComputeFrameworkModule::GetComputeSystem()->GetComputeWorker(InScene);
	if (!ensure(ComputeGraphWorker))
	{
		return false;
	}

	// Don't submit work if we don't have all of the expected bindings.
	// If we hit the ensure then something invalidated providers without calling CreateDataProviders().
	// Those paths DO need fixing. We can remove the ensure() if we ever feel safe enough!
	const bool bValidProviders = InComputeGraph->ValidateProviders(DataProviders);
	if (!ensure(bValidProviders))
	{
		return false;
	}

	TArray<FComputeDataProviderRenderProxy*> ComputeDataProviderProxies;
	for (UComputeDataProvider* DataProvider : DataProviders)
	{
		// Be sure to add null provider slots because we want to maintain consistent array indices.
		// Note that we expect GetRenderProxy() to return a pointer that we can own and call delete on.
		FComputeDataProviderRenderProxy* ProviderProxy = DataProvider != nullptr ? DataProvider->GetRenderProxy() : nullptr;
		ComputeDataProviderProxies.Add(ProviderProxy);
	}

	FComputeGraphProxy* ComputeGraphProxy = new FComputeGraphProxy();
	ComputeGraphProxy->Initialize(InComputeGraph);

	ENQUEUE_RENDER_COMMAND(ComputeFrameworkEnqueueExecutionCommand)(
		[ComputeGraphWorker, ComputeGraphProxy, DataProviderProxies = MoveTemp(ComputeDataProviderProxies)](FRHICommandListImmediate& RHICmdList)
		{
			// Compute graph scheduler will take ownership of the provider proxies.
			ComputeGraphWorker->Enqueue(ComputeGraphProxy, DataProviderProxies);
			delete ComputeGraphProxy;
		});

	return true;
}
