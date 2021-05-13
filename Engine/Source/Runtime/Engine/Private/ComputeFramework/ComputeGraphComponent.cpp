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
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UComputeGraphComponent::SetDataProvider(int32 Index, UComputeDataProvider* DataProvider)
{
	if (DataProviders.Num() <= Index)
	{
		DataProviders.SetNumZeroed(Index + 1);
	}
	DataProviders[Index] = DataProvider;
}

void UComputeGraphComponent::QueueExecute()
{
	if (ComputeGraph == nullptr || GetScene() == nullptr || GetScene()->GetComputeGraphScheduler() == nullptr)
	{
		return;
	}

	MarkRenderDynamicDataDirty();
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
		[ComputeGraphScheduler, ComputeGraphProxy, ProviderProxies = MoveTemp(ComputeDataProviderProxies)](FRHICommandListImmediate& RHICmdList)
		{
			ComputeGraphScheduler->EnqueueForExecution(ComputeGraphProxy, ProviderProxies);
			
			for (FComputeDataProviderRenderProxy* ProviderProxy : ProviderProxies)
			{
				delete ProviderProxy;
			}

			delete ComputeGraphProxy;
		});
}
