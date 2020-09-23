// Copyright Epic Games, Inc. All Rights Reserved.
#include "InterchangeTaskPipeline.h"

#include "Async/TaskGraphInterfaces.h"
#include "CoreMinimal.h"
#include "InterchangeManager.h"
#include "InterchangePipelineBase.h"
#include "Stats/Stats.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Nodes/InterchangeBaseNodeContainer.h"

void Interchange::FTaskPipeline::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	TSharedPtr<Interchange::FImportAsyncHelper, ESPMode::ThreadSafe> AsyncHelper = WeakAsyncHelper.Pin();
	check(AsyncHelper.IsValid());

	if (UInterchangePipelineBase* Pipeline = PipelineBase.Get())
	{
		for (int32 GraphIndex = 0; GraphIndex < AsyncHelper->BaseNodeContainers.Num(); ++GraphIndex)
		{
			check(AsyncHelper->BaseNodeContainers[GraphIndex].IsValid());
			Pipeline->ScriptedExecuteImportPipeline(AsyncHelper->BaseNodeContainers[GraphIndex].Get());
		}
	}
}
