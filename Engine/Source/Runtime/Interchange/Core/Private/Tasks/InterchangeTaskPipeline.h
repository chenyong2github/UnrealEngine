// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/TaskGraphInterfaces.h"
#include "CoreMinimal.h"
#include "InterchangeManager.h"
#include "InterchangePipelineBase.h"
#include "Stats/Stats.h"
#include "UObject/WeakObjectPtrTemplates.h"

namespace Interchange
{

class FTaskPipeline
{
private:
	TWeakObjectPtr<UInterchangePipelineBase> PipelineBase;
	TWeakPtr<Interchange::FImportAsyncHelper, ESPMode::ThreadSafe> WeakAsyncHelper;
public:
	FTaskPipeline(TWeakObjectPtr<UInterchangePipelineBase> InPipelineBase, TWeakPtr<Interchange::FImportAsyncHelper, ESPMode::ThreadSafe> InAsyncHelper)
		: PipelineBase(InPipelineBase)
		, WeakAsyncHelper(InAsyncHelper)
	{
	}

	ENamedThreads::Type GetDesiredThread()
	{
		return PipelineBase.Get()->ScriptedCanExecuteOnAnyThread() ? ENamedThreads::AnyBackgroundThreadNormalTask : ENamedThreads::GameThread;
	}
	
	static ESubsequentsMode::Type GetSubsequentsMode()
	{
		return ESubsequentsMode::TrackSubsequents;
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FTaskPipeline, STATGROUP_TaskGraphTasks);
	}

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent);
};


} // End namespace Interchange
