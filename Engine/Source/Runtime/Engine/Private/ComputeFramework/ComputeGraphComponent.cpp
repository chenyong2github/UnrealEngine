// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeGraphComponent.h"

#include "ComputeFramework/ComputeDataInterface.h"
#include "ComputeFramework/ComputeDataProvider.h"
#include "ComputeFramework/ComputeGraph.h"
#include "ComputeFramework/ComputeGraphScheduler.h"
#include "SceneInterface.h"

UComputeGraphComponent::UComputeGraphComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// By default don't tick and allow any queuing of work to be handled by blueprint.
	// Ticking can be turned on by some systems that need it (such as editor window).
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UComputeGraphComponent::CreateDataProviders(bool bSetDefaultBindings)
{
	DataProviders.Reset();
	if (ComputeGraph != nullptr)
	{
		ComputeGraph->CreateDataProviders(this, bSetDefaultBindings, DataProviders);
	}
}

void UComputeGraphComponent::QueueExecute()
{
	if (ComputeGraph == nullptr || GetScene() == nullptr || GetScene()->GetComputeGraphScheduler() == nullptr)
	{
		return;
	}

	MarkRenderDynamicDataDirty();
}

void UComputeGraphComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	QueueExecute();
}

void UComputeGraphComponent::SendRenderDynamicData_Concurrent()
{
	Super::SendRenderDynamicData_Concurrent();
	
	if (!ensure(ComputeGraph))
	{
		return;
	}

	FSceneInterface* Scene = GetScene();
	FComputeGraphScheduler* ComputeGraphScheduler = Scene != nullptr ? Scene->GetComputeGraphScheduler() : nullptr;
	if (!ensure(ComputeGraphScheduler))
	{
		return;
	}

	FComputeGraphProxy* ComputeGraphProxy = new FComputeGraphProxy();
	ComputeGraphProxy->Initialize(ComputeGraph);

	TArray<FComputeDataProviderRenderProxy*> ComputeDataProviderProxies;
	for (UComputeDataProvider* DataProvider : DataProviders)
	{
		// Add null provider slots because we want to maintain consistent array indices.
		FComputeDataProviderRenderProxy* ProviderProxy = DataProvider != nullptr ? DataProvider->GetRenderProxy() : nullptr;
		ComputeDataProviderProxies.Add(ProviderProxy);
	}

	ENQUEUE_RENDER_COMMAND(ComputeFrameworkEnqueueExecutionCommand)(
		[ComputeGraphScheduler, ComputeGraphProxy, DataProviderProxies = MoveTemp(ComputeDataProviderProxies)](FRHICommandListImmediate& RHICmdList)
		{
			// Compute graph scheduler will take ownership of the provider proxies.
			ComputeGraphScheduler->EnqueueForExecution(ComputeGraphProxy, DataProviderProxies);
			delete ComputeGraphProxy;
		});
}
