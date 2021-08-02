// Copyright Epic Games, Inc. All Rights Reserved.
#include "InterchangeTaskCompletion.h"

#include "AssetRegistryModule.h"
#include "Async/TaskGraphInterfaces.h"
#include "CoreMinimal.h"
#include "InterchangeEngineLogPrivate.h"
#include "InterchangeFactoryBase.h"
#include "InterchangeManager.h"
#include "InterchangeResultsContainer.h"
#include "Stats/Stats.h"
#include "Templates/SharedPointer.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/WeakObjectPtrTemplates.h"

void UE::Interchange::FTaskPreAsyncCompletion::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
#if INTERCHANGE_TRACE_ASYNCHRONOUS_TASK_ENABLED
	INTERCHANGE_TRACE_ASYNCHRONOUS_TASK(PreAsyncCompletion)
#endif
	TSharedPtr<FImportAsyncHelper, ESPMode::ThreadSafe> AsyncHelper = WeakAsyncHelper.Pin();
	check(AsyncHelper.IsValid());

	//No need anymore of the translators sources
	AsyncHelper->ReleaseTranslatorsSource();
}

void UE::Interchange::FTaskPreCompletion::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
#if INTERCHANGE_TRACE_ASYNCHRONOUS_TASK_ENABLED
	INTERCHANGE_TRACE_ASYNCHRONOUS_TASK(PreCompletion)
#endif
	TSharedPtr<FImportAsyncHelper, ESPMode::ThreadSafe> AsyncHelper = WeakAsyncHelper.Pin();
	check(AsyncHelper.IsValid());

	UInterchangeResultsContainer* Results = AsyncHelper->AssetImportResult->GetResults();

	for (TPair<int32, TArray<FImportAsyncHelper::FImportedAssetInfo>>& AssetInfosPerSourceIndexPair : AsyncHelper->ImportedAssetsPerSourceIndex)
	{
		//Verify if the task was cancel
		if (AsyncHelper->bCancel)
		{
			break;
		}

		const int32 SourceIndex = AssetInfosPerSourceIndexPair.Key;
		const bool bCallPostImportGameThreadCallback = ensure(AsyncHelper->SourceDatas.IsValidIndex(SourceIndex));

		for (const FImportAsyncHelper::FImportedAssetInfo& AssetInfo : AssetInfosPerSourceIndexPair.Value)
		{
			UObject* Asset = AssetInfo.ImportAsset;
			//In case Some factory code cannot run outside of the main thread we offer this callback to finish the work before calling post edit change (building the asset)
			if (bCallPostImportGameThreadCallback && AssetInfo.Factory)
			{
				UInterchangeFactoryBase::FImportPreCompletedCallbackParams Arguments;
				Arguments.ImportedObject = Asset;
				Arguments.SourceData = AsyncHelper->SourceDatas[SourceIndex];
				Arguments.FactoryNode = AssetInfo.FactoryNode;
				// Should we assert if there is no factory node?
				Arguments.NodeUniqueID = AssetInfo.FactoryNode ? AssetInfo.FactoryNode->GetUniqueID() : FString();
				Arguments.NodeContainer = AsyncHelper->BaseNodeContainers[SourceIndex].Get();
				Arguments.Pipelines = AsyncHelper->Pipelines;
				Arguments.bIsReimport = AssetInfo.bIsReimport;
				AssetInfo.Factory->PreImportPreCompletedCallback(Arguments);
			}

			if (Asset == nullptr)
			{
				continue;
			}

			UInterchangeResultSuccess* Message = Results->Add<UInterchangeResultSuccess>();
			Message->SourceAssetName = AsyncHelper->SourceDatas[SourceIndex]->GetFilename();
			Message->DestinationAssetName = Asset->GetPathName();
			Message->AssetType = Asset->GetClass();

			//Clear any async flag from the created asset
			const EInternalObjectFlags AsyncFlags = EInternalObjectFlags::Async | EInternalObjectFlags::AsyncLoading;
			Asset->ClearInternalFlags(AsyncFlags);
			//Make sure the package is dirty
			Asset->MarkPackageDirty();
#if WITH_EDITOR
			//Make sure the asset is built correctly
			Asset->PostEditChange();
#endif
			//Post import broadcast
			if (!AsyncHelper->TaskData.ReimportObject)
			{
				//Notify the asset registry, only when we have created the asset
				FAssetRegistryModule::AssetCreated(Asset);
			}
			AsyncHelper->AssetImportResult->AddImportedAsset(Asset);
			//In case Some factory code cannot run outside of the main thread we offer this callback to finish the work after calling post edit change (building the asset)
			//Its possible the build of the asset to be asynchronous, the factory must handle is own asset correctly
			if (bCallPostImportGameThreadCallback && AssetInfo.Factory)
			{
				UInterchangeFactoryBase::FImportPreCompletedCallbackParams Arguments;
				Arguments.ImportedObject = Asset;
				Arguments.SourceData = AsyncHelper->SourceDatas[SourceIndex];
				Arguments.FactoryNode = AssetInfo.FactoryNode;
				Arguments.NodeUniqueID = AssetInfo.FactoryNode ? AssetInfo.FactoryNode->GetUniqueID() : FString();
				Arguments.NodeContainer = AsyncHelper->BaseNodeContainers[SourceIndex].Get();
				Arguments.Pipelines = AsyncHelper->Pipelines;
				Arguments.bIsReimport = AssetInfo.bIsReimport;
				AssetInfo.Factory->PostImportPreCompletedCallback(Arguments);
			}
		}
	}
}


void UE::Interchange::FTaskCompletion::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
#if INTERCHANGE_TRACE_ASYNCHRONOUS_TASK_ENABLED
	INTERCHANGE_TRACE_ASYNCHRONOUS_TASK(Completion)
#endif
	TSharedPtr<FImportAsyncHelper, ESPMode::ThreadSafe> AsyncHelper = WeakAsyncHelper.Pin();
	check(AsyncHelper.IsValid());

	//No need anymore of the translators sources
	AsyncHelper->ReleaseTranslatorsSource();
	for(TPair<int32, TArray<FImportAsyncHelper::FImportedAssetInfo>>& AssetInfosPerSourceIndexPair : AsyncHelper->ImportedAssetsPerSourceIndex)
	{
		//Verify if the task was cancel
		if (AsyncHelper->bCancel)
		{
			break;
		}
		const int32 SourceIndex = AssetInfosPerSourceIndexPair.Key;
		for (const FImportAsyncHelper::FImportedAssetInfo& AssetInfo : AssetInfosPerSourceIndexPair.Value)
		{
			UObject* Asset = AssetInfo.ImportAsset;
			if (AsyncHelper->TaskData.ReimportObject)
			{
				InterchangeManager->OnAssetPostReimport.Broadcast(Asset);
			}
			else
			{
				InterchangeManager->OnAssetPostImport.Broadcast(Asset);
			}
		}
	}

	//If task is cancel, delete all created assets by this task
	if (AsyncHelper->bCancel)
	{
		for (TPair<int32, TArray<FImportAsyncHelper::FImportedAssetInfo>>& AssetInfosPerSourceIndexPair : AsyncHelper->ImportedAssetsPerSourceIndex)
		{
			const int32 SourceIndex = AssetInfosPerSourceIndexPair.Key;
			for (const FImportAsyncHelper::FImportedAssetInfo& AssetInfo : AssetInfosPerSourceIndexPair.Value)
			{
				UObject* Asset = AssetInfo.ImportAsset;
				if (Asset)
				{
					//Make any created asset go away
					Asset->ClearFlags(RF_Standalone | RF_Public | RF_Transactional);
					Asset->ClearInternalFlags(EInternalObjectFlags::Async);
					Asset->SetFlags(RF_Transient);
					Asset->MarkPendingKill();
				}
			}
		}
	}

	AsyncHelper->AssetImportResult->SetDone();

	//Release the async helper
	AsyncHelper = nullptr;
	InterchangeManager->ReleaseAsyncHelper(WeakAsyncHelper);
}
