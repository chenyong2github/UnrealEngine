// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeGraphComponent.h"
#include "ComputeFramework/ComputeGraph.h"
#include "ComputeFramework/ComputeFramework.h"
#include "SceneInterface.h"


UComputeGraphComponent::UComputeGraphComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UComputeGraphComponent::QueueExecute()
{
	if (!ComputeGraph || !GetScene()->GetComputeFramework())
	{
		return;
	}

	MarkRenderDynamicDataDirty();
}

void UComputeGraphComponent::SendRenderDynamicData_Concurrent()
{
	check(ComputeGraph);
	Super::SendRenderDynamicData_Concurrent();
	
	FComputeFramework* ComputeFramework = GetScene()->GetComputeFramework();
	check(ComputeFramework);

	FComputeGraph* ComputeGraphProxy = new FComputeGraph();
	ComputeGraphProxy->Initialize(ComputeGraph);

	ENQUEUE_RENDER_COMMAND(ComputeFrameworkEnqueueExecutionCommand)(
		[ComputeFramework, ComputeGraphProxy](FRHICommandListImmediate& RHICmdList)
		{
			ComputeFramework->EnqueueForExecution(ComputeGraphProxy);
			delete ComputeGraphProxy;
		});
}
