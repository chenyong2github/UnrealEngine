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
		FString UniqueID;
		int32 SourceIndex;
		UInterchangeBaseNode* Node;
		TArray<FString> Dependencies;
		FGraphEventRef GraphEventRef;
		FGraphEventArray Prerequistes;
		const UClass* FactoryClass;
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
			BaseNodeContainer->IterateNodes([&](const FString& NodeUID, UInterchangeBaseNode* Node)
			{
				if (Node->GetAssetClass() != nullptr)
				{
					const UClass* RegisteredFactoryClass = InterchangeManager->GetRegisteredFactoryClass(Node->GetAssetClass());
					if (!RegisteredFactoryClass)
					{
						//nothing we can import from this element
						return;
					}

					FTaskData& NodeTaskData = TaskDatas.AddDefaulted_GetRef();
					NodeTaskData.UniqueID = Node->GetUniqueID();
					NodeTaskData.SourceIndex = SourceIndex;
					NodeTaskData.Node = Node;
					Node->GetFactoryDependencies(NodeTaskData.Dependencies);
					NodeTaskData.FactoryClass = RegisteredFactoryClass;
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

		const int32 SourceIndex = TaskDatas[TaskIndex].SourceIndex;
		const UClass* const FactoryClass = TaskDatas[TaskIndex].FactoryClass;
		UInterchangeBaseNode* const FactoryNode = TaskDatas[TaskIndex].Node;
		const bool bFactoryCanRunOnAnyThread = FactoryClass->GetDefaultObject<UInterchangeFactoryBase>()->CanExecuteOnAnyThread();

		//Add create package task has a prerequisite of FTaskCreateAsset. Create package task is a game thread task
		FGraphEventArray CreatePackagePrerequistes;
		int32 CreatePackageTaskIndex = AsyncHelper->CreatePackageTasks.Add(
			TGraphTask<FTaskCreatePackage>::CreateTask(&(TaskDatas[TaskIndex].Prerequistes)).ConstructAndDispatchWhenReady(PackageBasePath, SourceIndex, WeakAsyncHelper, FactoryNode, FactoryClass)
		);
		CreatePackagePrerequistes.Add(AsyncHelper->CreatePackageTasks[CreatePackageTaskIndex]);

		FGraphEventArray PostPipelinePrerequistes;
		int32 CreateTaskIndex = AsyncHelper->CreateAssetTasks.Add(
			TGraphTask<FTaskCreateAsset>::CreateTask(&(CreatePackagePrerequistes)).ConstructAndDispatchWhenReady(PackageBasePath, SourceIndex, WeakAsyncHelper, FactoryNode, bFactoryCanRunOnAnyThread)
		);
		PostPipelinePrerequistes.Add(AsyncHelper->CreateAssetTasks[CreateTaskIndex]);
		
		TaskDatas[TaskIndex].GraphEventRef = AsyncHelper->CreateAssetTasks[CreateTaskIndex];

		for (int32 GraphPipelineIndex = 0; GraphPipelineIndex < AsyncHelper->Pipelines.Num(); ++GraphPipelineIndex)
		{
			int32 GraphPipelineTaskIndex = INDEX_NONE;
			GraphPipelineTaskIndex = AsyncHelper->PipelinePostImportTasks.Add(
				TGraphTask<FTaskPipelinePostImport>::CreateTask(&(PostPipelinePrerequistes)).ConstructAndDispatchWhenReady(SourceIndex, GraphPipelineIndex, WeakAsyncHelper)
			);
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
