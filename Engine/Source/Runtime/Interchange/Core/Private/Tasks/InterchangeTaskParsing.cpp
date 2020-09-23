// Copyright Epic Games, Inc. All Rights Reserved.
#include "InterchangeTaskParsing.h"

#include "Async/TaskGraphInterfaces.h"
#include "CoreMinimal.h"
#include "InterchangeFactoryBase.h"
#include "InterchangeManager.h"
#include "InterchangeSourceData.h"
#include "InterchangeTaskCompletion.h"
#include "InterchangeTaskCreateAsset.h"
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

void Interchange::FTaskParsing::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	TSharedPtr<Interchange::FImportAsyncHelper, ESPMode::ThreadSafe> AsyncHelper = WeakAsyncHelper.Pin();
	check(AsyncHelper.IsValid());

	//Parse each graph and prepare import task data, we will then be able to create all the task with the correct dependencies
	struct FTaskData
	{
		FName UniqueID;
		int32 SourceIndex;
		const UInterchangeBaseNode* Node;
		TArray<FName> Dependencies;
		FGraphEventRef GraphEventRef;
		FGraphEventArray Prerequistes;
		UInterchangeFactoryBase* Factory;
	};



	FGraphEventArray CompletionPrerequistes;
	TArray<FTaskData> TaskDatas;
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
		BaseNodeContainer->IterateNodes([&](const FName NodeUID, const UInterchangeBaseNode* Node)
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
		CreatePackagePrerequistes.Add(TGraphTask<FTaskCreatePackage>::CreateTask(&(TaskDatas[TaskIndex].Prerequistes)).ConstructAndDispatchWhenReady(PackageBasePath, TaskDatas[TaskIndex].SourceIndex, WeakAsyncHelper, TaskDatas[TaskIndex].Node, TaskDatas[TaskIndex].Factory));

		int32 CreateTaskIndex = AsyncHelper->CreateAssetTasks.Add(TGraphTask<FTaskCreateAsset>::CreateTask(&(CreatePackagePrerequistes)).ConstructAndDispatchWhenReady(PackageBasePath, TaskDatas[TaskIndex].SourceIndex, WeakAsyncHelper, TaskDatas[TaskIndex].Node, TaskDatas[TaskIndex].Factory));
		TaskDatas[TaskIndex].GraphEventRef = AsyncHelper->CreateAssetTasks[CreateTaskIndex];
		CompletionPrerequistes.Add(TaskDatas[TaskIndex].GraphEventRef);
	}

	AsyncHelper->CompletionTask = TGraphTask<FTaskCompletion>::CreateTask(&CompletionPrerequistes).ConstructAndDispatchWhenReady(InterchangeManager, WeakAsyncHelper);
}
