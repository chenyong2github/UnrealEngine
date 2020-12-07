// Copyright Epic Games, Inc. All Rights Reserved.
#include "InterchangeTaskParsing.h"

#include "Async/TaskGraphInterfaces.h"
#include "CoreMinimal.h"
#include "InterchangeEngineLogPrivate.h"
#include "InterchangeFactoryBase.h"
#include "InterchangeManager.h"
#include "InterchangeSourceData.h"
#include "InterchangeTaskCompletion.h"
#include "InterchangeTaskCreateAsset.h"
#include "InterchangeTaskPipeline.h"
#include "InterchangeTranslatorBase.h"
#include "Misc/Paths.h"
#include "PackageUtils/PackageUtils.h"
#include "Stats/Stats.h"
#include "Templates/SharedPointer.h"
#include "UObject/Object.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/ObjectMacros.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"

void UE::Interchange::FTaskParsing::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
#if INTERCHANGE_TRACE_ASYNCHRONOUS_TASK_ENABLED
	INTERCHANGE_TRACE_ASYNCHRONOUS_TASK(ParsingGraph)
#endif
	TSharedPtr<FImportAsyncHelper, ESPMode::ThreadSafe> AsyncHelper = WeakAsyncHelper.Pin();
	check(AsyncHelper.IsValid());

	//Parse each graph and prepare import task data, we will then be able to create all the task with the correct dependencies
	struct FTaskData
	{
		FName UniqueID;
		int32 SourceIndex;
		UInterchangeBaseNode* Node;
		TArray<FName> Dependencies;
		FGraphEventRef GraphEventRef;
		FGraphEventArray Prerequistes;
		UInterchangeFactoryBase* Factory;
	};


	FGraphEventArray CompletionPrerequistes;
	TArray<FTaskData> TaskDatas;

	//Avoid creating asset if the asynchronous import is cancel, just create the completion task
	if (!AsyncHelper->bCancel)
	{
		for (int32 SourceIndex = 0; SourceIndex < AsyncHelper->SourceDatas.Num(); ++SourceIndex)
		{
			if (!AsyncHelper->BaseNodeContainers.IsValidIndex(SourceIndex))
			{
				continue;
			}
			UInterchangeBaseNodeContainer* BaseNodeContainer = AsyncHelper->BaseNodeContainers[SourceIndex].Get();
			if (!BaseNodeContainer)
			{
				continue;
			}
			BaseNodeContainer->IterateNodes([&](const FName NodeUID, UInterchangeBaseNode* Node)
			{
				if (Node->GetAssetClass() != nullptr)
				{
					UInterchangeFactoryBase* NodeFactory = InterchangeManager->GetRegisterFactory(Node->GetAssetClass());
					if (!NodeFactory)
					{
						//nothing we can import from this element
						return;
					}
					FTaskData& NodeTaskData = TaskDatas.AddDefaulted_GetRef();
					NodeTaskData.UniqueID = Node->GetUniqueID();
					NodeTaskData.SourceIndex = SourceIndex;
					NodeTaskData.Node = Node;
					Node->GetDependecies(NodeTaskData.Dependencies);
					NodeTaskData.Factory = NodeFactory;
				}
			});
		}
	}
	//Sort per dependencies
	TaskDatas.Sort([](const FTaskData& A, const FTaskData& B)
	{
		//if A is a dependency of B then return true to do A before B
		if (B.Dependencies.Contains(A.UniqueID))
		{
			return true;
		}
		if (A.Dependencies.Contains(B.UniqueID))
		{
			return false;
		}
		return A.Dependencies.Num() <= B.Dependencies.Num();
	});

	for (int32 TaskIndex = 0; TaskIndex < TaskDatas.Num(); ++TaskIndex)
	{
		if (TaskDatas[TaskIndex].Dependencies.Num() > 0)
		{
			//Search the previous node to find the dependence
			for (int32 DepTaskIndex = 0; DepTaskIndex < TaskIndex; ++DepTaskIndex)
			{
				if (TaskDatas[TaskIndex].Dependencies.Contains(TaskDatas[DepTaskIndex].UniqueID))
				{
					//Add has prerequisite
					TaskDatas[TaskIndex].Prerequistes.Add(TaskDatas[DepTaskIndex].GraphEventRef);
				}
			}
		}

		//Add create package task has a prerequisite of FTaskCreateAsset. Create package task is a game thread task
		FGraphEventArray CreatePackagePrerequistes;
		int32 CreatePackageTaskIndex = AsyncHelper->CreatePackageTasks.Add(TGraphTask<FTaskCreatePackage>::CreateTask(&(TaskDatas[TaskIndex].Prerequistes)).ConstructAndDispatchWhenReady(PackageBasePath, TaskDatas[TaskIndex].SourceIndex, WeakAsyncHelper, TaskDatas[TaskIndex].Node, TaskDatas[TaskIndex].Factory));
		CreatePackagePrerequistes.Add(AsyncHelper->CreatePackageTasks[CreatePackageTaskIndex]);

		FGraphEventArray PostPipelinePrerequistes;
		int32 CreateTaskIndex = AsyncHelper->CreateAssetTasks.Add(TGraphTask<FTaskCreateAsset>::CreateTask(&(CreatePackagePrerequistes)).ConstructAndDispatchWhenReady(PackageBasePath, TaskDatas[TaskIndex].SourceIndex, WeakAsyncHelper, TaskDatas[TaskIndex].Node, TaskDatas[TaskIndex].Factory));
		PostPipelinePrerequistes.Add(AsyncHelper->CreateAssetTasks[CreateTaskIndex]);
		
		TaskDatas[TaskIndex].GraphEventRef = AsyncHelper->CreateAssetTasks[CreateTaskIndex];

		for (int32 GraphPipelineIndex = 0; GraphPipelineIndex < AsyncHelper->Pipelines.Num(); ++GraphPipelineIndex)
		{
			int32 GraphPipelineTaskIndex = INDEX_NONE;
			GraphPipelineTaskIndex = AsyncHelper->PipelinePostImportTasks.Add(TGraphTask<FTaskPipelinePostImport>::CreateTask(&(PostPipelinePrerequistes)).ConstructAndDispatchWhenReady(TaskDatas[TaskIndex].SourceIndex, GraphPipelineIndex, WeakAsyncHelper));
			//Ensure we run the pipeline in the same order we create the task, since pipeline modify the node container, its important that its not process in parallel, Adding the one we start to the prerequisites
			//is the way to go here
			PostPipelinePrerequistes.Add(AsyncHelper->PipelinePostImportTasks[GraphPipelineTaskIndex]);

			//Override the completion prerequisite with the latest post import pipeline task
			TaskDatas[TaskIndex].GraphEventRef = AsyncHelper->PipelinePostImportTasks[GraphPipelineTaskIndex];
		}

		CompletionPrerequistes.Add(TaskDatas[TaskIndex].GraphEventRef);
	}

	FGraphEventArray PreAsyncCompletionPrerequistes;
	AsyncHelper->PreAsyncCompletionTask = TGraphTask<FTaskPreAsyncCompletion>::CreateTask(&CompletionPrerequistes).ConstructAndDispatchWhenReady(InterchangeManager, WeakAsyncHelper);
	PreAsyncCompletionPrerequistes.Add(AsyncHelper->PreAsyncCompletionTask);

	AsyncHelper->CompletionTask = TGraphTask<FTaskCompletion>::CreateTask(&PreAsyncCompletionPrerequistes).ConstructAndDispatchWhenReady(InterchangeManager, WeakAsyncHelper);
}
