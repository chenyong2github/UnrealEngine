// Copyright Epic Games, Inc. All Rights Reserved.
#include "InterchangeTaskParsing.h"

#include "Async/TaskGraphInterfaces.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InterchangeEngineLogPrivate.h"
#include "InterchangeFactoryBase.h"
#include "InterchangeManager.h"
#include "InterchangeSourceData.h"
#include "InterchangeTaskCompletion.h"
#include "InterchangeTaskCreateAsset.h"
#include "InterchangeTaskCreateSceneObjects.h"
#include "InterchangeTaskPipeline.h"
#include "InterchangeTranslatorBase.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "PackageUtils/PackageUtils.h"
#include "Stats/Stats.h"
#include "Templates/SharedPointer.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/WeakObjectPtrTemplates.h"


void UE::Interchange::FTaskParsing::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
#if INTERCHANGE_TRACE_ASYNCHRONOUS_TASK_ENABLED
	INTERCHANGE_TRACE_ASYNCHRONOUS_TASK(ParsingGraph)
#endif
	FGCScopeGuard GCScopeGuard;

	TSharedPtr<FImportAsyncHelper, ESPMode::ThreadSafe> AsyncHelper = WeakAsyncHelper.Pin();
	check(AsyncHelper.IsValid());

	//Parse each graph and prepare import task data, we will then be able to create all the task with the correct dependencies
	struct FTaskData
	{
		FString UniqueID;
		int32 SourceIndex = INDEX_NONE;
		TArray<FString> Dependencies;
		FGraphEventRef GraphEventRef;
		FGraphEventArray Prerequisites;
		const UClass* FactoryClass;

		TArray<UInterchangeBaseNode*, TInlineAllocator<1>> Nodes; // For scenes, we can group multiple nodes into a single task as they are usually very light
	};

	TArray<FTaskData> AssetTaskDatas;
	TArray<FTaskData> SceneTaskDatas;

	//Avoid creating asset if the asynchronous import is canceled, just create the completion task
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
				if (!Node->IsEnabled())
				{
					//Do not call factory for a disabled node
					return;
				}

				if (Node->GetObjectClass() != nullptr)
				{
					const UClass* RegisteredFactoryClass = InterchangeManager->GetRegisteredFactoryClass(Node->GetObjectClass());

					const bool bIsAsset = !(Node->GetObjectClass()->IsChildOf<AActor>() || Node->GetObjectClass()->IsChildOf<UActorComponent>());
					const bool bCanImportSceneNode = [&AsyncHelper, &Node]()
					{
						bool bCanImport = AsyncHelper->TaskData.ImportType == EImportType::ImportType_Scene;
						bCanImport = bCanImport && Node->GetParentUid().IsEmpty(); // We only import root scene nodes since we ask the factory to spawn the children nodes

						return bCanImport;
					}();

					if (!RegisteredFactoryClass || (!bIsAsset && !bCanImportSceneNode))
					{
						//nothing we can import from this element
						return;
					}

					FTaskData& NodeTaskData = bIsAsset ? AssetTaskDatas.AddDefaulted_GetRef() : SceneTaskDatas.AddDefaulted_GetRef();
					NodeTaskData.UniqueID = Node->GetUniqueID();
					NodeTaskData.SourceIndex = SourceIndex;
					NodeTaskData.Nodes.Add(Node);
					Node->GetFactoryDependencies(NodeTaskData.Dependencies);
					NodeTaskData.FactoryClass = RegisteredFactoryClass;
				}
			});
		}
	}
	//Sort per dependencies
	auto SortByDependencies =
		[](const FTaskData& A, const FTaskData& B)
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
		};

	AssetTaskDatas.Sort(SortByDependencies);
	SceneTaskDatas.Sort(SortByDependencies);

	auto CreateTasksForEachTaskData = [](TArray<FTaskData>& TaskDatas, TFunction<FGraphEventRef(FTaskData&)> CreateTasksFunc) -> FGraphEventArray
	{
		FGraphEventArray GraphEvents;

		for (int32 TaskIndex = 0; TaskIndex < TaskDatas.Num(); ++TaskIndex)
		{
			FTaskData& TaskData = TaskDatas[TaskIndex];

			if (TaskData.Dependencies.Num() > 0)
			{
				//Search the previous node to find the dependence
				for (int32 DepTaskIndex = 0; DepTaskIndex < TaskIndex; ++DepTaskIndex)
				{
					if (TaskData.Dependencies.Contains(TaskDatas[DepTaskIndex].UniqueID))
					{
						//Add has prerequisite
						TaskData.Prerequisites.Add(TaskDatas[DepTaskIndex].GraphEventRef);
					}
				}
			}

			TaskData.GraphEventRef = CreateTasksFunc(TaskData);
			GraphEvents.Add(TaskData.GraphEventRef);
		}

		return GraphEvents;
	};

	//Assets
	FGraphEventArray AssetsCompletionPrerequistes;
	{
		TFunction<FGraphEventRef(FTaskData&)> CreateTasksForAsset = [this, &AsyncHelper](FTaskData& TaskData)
		{
			check(TaskData.Nodes.Num() == 1); //We expect 1 node per asset task

			const int32 SourceIndex = TaskData.SourceIndex;
			const UClass* const FactoryClass = TaskData.FactoryClass;
			UInterchangeBaseNode* const FactoryNode = TaskData.Nodes[0];
			const bool bFactoryCanRunOnAnyThread = FactoryClass->GetDefaultObject<UInterchangeFactoryBase>()->CanExecuteOnAnyThread();

			//Add create package task has a prerequisite of FTaskCreateAsset. Create package task is a game thread task
			FGraphEventArray CreatePackagePrerequistes;
			int32 CreatePackageTaskIndex = AsyncHelper->CreatePackageTasks.Add(
				TGraphTask<FTaskCreatePackage>::CreateTask(&(TaskData.Prerequisites)).ConstructAndDispatchWhenReady(PackageBasePath, SourceIndex, WeakAsyncHelper, FactoryNode, FactoryClass)
			);
			CreatePackagePrerequistes.Add(AsyncHelper->CreatePackageTasks[CreatePackageTaskIndex]);

			FGraphEventArray CreateAssetPrerequistes;
			int32 CreateTaskIndex = AsyncHelper->CreateAssetTasks.Add(
				TGraphTask<FTaskCreateAsset>::CreateTask(&(CreatePackagePrerequistes)).ConstructAndDispatchWhenReady(PackageBasePath, SourceIndex, WeakAsyncHelper, FactoryNode, bFactoryCanRunOnAnyThread)
			);
			CreateAssetPrerequistes.Add(AsyncHelper->CreateAssetTasks[CreateTaskIndex]);

			return AsyncHelper->CreateAssetTasks[CreateTaskIndex];
		};

		AssetsCompletionPrerequistes = CreateTasksForEachTaskData(AssetTaskDatas, CreateTasksForAsset);
	}

	//Scenes
	//Note: Scene tasks are delayed until all asset tasks are completed
	FGraphEventArray ScenesCompletionPrerequistes;
	{
		TFunction<FGraphEventRef(FTaskData&)> CreateTasksForSceneObject = [this, &AsyncHelper, &AssetsCompletionPrerequistes](FTaskData& TaskData)
		{
			const bool bSpawnChildren = true;
			const int32 SourceIndex = TaskData.SourceIndex;
			const UClass* const FactoryClass = TaskData.FactoryClass;

			return AsyncHelper->SceneTasks.Add_GetRef(
				TGraphTask<FTaskCreateSceneObjects>::CreateTask(&AssetsCompletionPrerequistes)
				.ConstructAndDispatchWhenReady(PackageBasePath, SourceIndex, WeakAsyncHelper, TaskData.Nodes, FactoryClass->GetDefaultObject<UInterchangeFactoryBase>(), bSpawnChildren));

		};

		ScenesCompletionPrerequistes = CreateTasksForEachTaskData(SceneTaskDatas, CreateTasksForSceneObject);
	}

	FGraphEventArray CompletionPrerequistes;
	CompletionPrerequistes.Append(AssetsCompletionPrerequistes);
	CompletionPrerequistes.Append(ScenesCompletionPrerequistes);

	//Add an async task for pre completion
	
	FGraphEventArray PreCompletionPrerequistes;
	AsyncHelper->PreCompletionTask = TGraphTask<FTaskPreCompletion>::CreateTask(&CompletionPrerequistes).ConstructAndDispatchWhenReady(InterchangeManager, WeakAsyncHelper);
	PreCompletionPrerequistes.Add(AsyncHelper->PreCompletionTask);

	//Start the Post pipeline task
	for (int32 SourceIndex = 0; SourceIndex < AsyncHelper->SourceDatas.Num(); ++SourceIndex)
	{
		for (int32 GraphPipelineIndex = 0; GraphPipelineIndex < AsyncHelper->Pipelines.Num(); ++GraphPipelineIndex)
		{
			int32 GraphPipelineTaskIndex = AsyncHelper->PipelinePostImportTasks.Add(
				TGraphTask<FTaskPipelinePostImport>::CreateTask(&(PreCompletionPrerequistes)).ConstructAndDispatchWhenReady(SourceIndex, GraphPipelineIndex, WeakAsyncHelper)
			);
			//Ensure we run the pipeline in the same order we create the task, since the pipeline modifies the node container, its important that its not processed in parallel, Adding the one we start to the prerequisites
			//is the way to go here
			PreCompletionPrerequistes.Add(AsyncHelper->PipelinePostImportTasks[GraphPipelineTaskIndex]);
		}
	}

	FGraphEventArray PreAsyncCompletionPrerequistes;
	AsyncHelper->PreAsyncCompletionTask = TGraphTask<FTaskPreAsyncCompletion>::CreateTask(&PreCompletionPrerequistes).ConstructAndDispatchWhenReady(InterchangeManager, WeakAsyncHelper);
	PreAsyncCompletionPrerequistes.Add(AsyncHelper->PreAsyncCompletionTask);

	AsyncHelper->CompletionTask = TGraphTask<FTaskCompletion>::CreateTask(&PreAsyncCompletionPrerequistes).ConstructAndDispatchWhenReady(InterchangeManager, WeakAsyncHelper);
}
