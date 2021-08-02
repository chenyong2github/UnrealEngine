// Copyright Epic Games, Inc. All Rights Reserved.
#include "InterchangeTaskPipeline.h"

#include "Async/TaskGraphInterfaces.h"
#include "CoreMinimal.h"
#include "InterchangeEngineLogPrivate.h"
#include "InterchangeManager.h"
#include "InterchangePipelineBase.h"
#include "Stats/Stats.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Nodes/InterchangeBaseNodeContainer.h"



void UE::Interchange::FTaskPipelinePreImport::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
#if INTERCHANGE_TRACE_ASYNCHRONOUS_TASK_ENABLED
	INTERCHANGE_TRACE_ASYNCHRONOUS_TASK(PipelinePreImport)
#endif
	TSharedPtr<FImportAsyncHelper, ESPMode::ThreadSafe> AsyncHelper = WeakAsyncHelper.Pin();
	check(AsyncHelper.IsValid());

	if (UInterchangePipelineBase* Pipeline = PipelineBase.Get())
	{
		Pipeline->SetResultsContainer(AsyncHelper->AssetImportResult->GetResults());

		for (int32 GraphIndex = 0; GraphIndex < AsyncHelper->BaseNodeContainers.Num(); ++GraphIndex)
		{
			//Verify if the task was cancel
			if (AsyncHelper->bCancel)
			{
				return;
			}

			if (ensure(AsyncHelper->BaseNodeContainers[GraphIndex].IsValid()))
			{
				Pipeline->ScriptedExecutePreImportPipeline(AsyncHelper->BaseNodeContainers[GraphIndex].Get(), AsyncHelper->SourceDatas);
			}
		}
	}
}

void UE::Interchange::FTaskPipelinePostImport::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
#if INTERCHANGE_TRACE_ASYNCHRONOUS_TASK_ENABLED
	INTERCHANGE_TRACE_ASYNCHRONOUS_TASK(PipelinePostImport)
#endif
	TSharedPtr<FImportAsyncHelper, ESPMode::ThreadSafe> AsyncHelper = WeakAsyncHelper.Pin();
	if (!ensure(AsyncHelper.IsValid()) || AsyncHelper->bCancel)
	{
		return;
	}

	if (!ensure(AsyncHelper->Pipelines.IsValidIndex(PipelineIndex)) || !ensure(AsyncHelper->BaseNodeContainers.IsValidIndex(SourceIndex)))
	{
		return;
	}
	UInterchangePipelineBase* PipelineBase = AsyncHelper->Pipelines[PipelineIndex];
	TArray<FString> NodeUniqueIDs;
	TArray<UObject*> ImportAssets;
	//Create a lock scope to read the Imported asset infos map
	{
		FScopeLock Lock(&AsyncHelper->ImportedAssetsPerSourceIndexLock);
		if (AsyncHelper->ImportedAssetsPerSourceIndex.Contains(SourceIndex))
		{
			TArray<UE::Interchange::FImportAsyncHelper::FImportedAssetInfo>& ImportedInfos = AsyncHelper->ImportedAssetsPerSourceIndex.FindChecked(SourceIndex);
			NodeUniqueIDs.Reserve(ImportedInfos.Num());
			ImportAssets.Reserve(ImportedInfos.Num());
			for (UE::Interchange::FImportAsyncHelper::FImportedAssetInfo& ImportedInfo : ImportedInfos)
			{
				NodeUniqueIDs.Add(ImportedInfo.FactoryNode->GetUniqueID());
				ImportAssets.Add(ImportedInfo.ImportAsset);
			}
		}
	}

	if (!ensure(NodeUniqueIDs.Num() == ImportAssets.Num()))
	{
		//We do not execute the script if we cannot give proper parameter
		return;
	}

	//Get the Container from the async helper
	UInterchangeBaseNodeContainer* NodeContainer = AsyncHelper->BaseNodeContainers[SourceIndex].Get();
	if (!ensure(NodeContainer))
	{
		return;
	}
	UInterchangePipelineBase* Pipeline = AsyncHelper->Pipelines[PipelineIndex];
	Pipeline->SetResultsContainer(AsyncHelper->AssetImportResult->GetResults());

	//Call the pipeline outside of the lock, we do this in case the pipeline take a long time. We call it for each asset created by this import
	for (int32 AssetIndex = 0; AssetIndex < ImportAssets.Num(); ++AssetIndex)
	{
		Pipeline->ScriptedExecutePostImportPipeline(NodeContainer, NodeUniqueIDs[AssetIndex], ImportAssets[AssetIndex]);
	}
}
