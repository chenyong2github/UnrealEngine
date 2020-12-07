// Copyright Epic Games, Inc. All Rights Reserved.
#include "InterchangeTaskCompletion.h"

#include "AssetRegistryModule.h"
#include "Async/TaskGraphInterfaces.h"
#include "CoreMinimal.h"
#include "InterchangeEngineLogPrivate.h"
#include "InterchangeFactoryBase.h"
#include "InterchangeManager.h"
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
		const bool bCallPostImportGameThreadCallback = ensure(AsyncHelper->SourceDatas.IsValidIndex(SourceIndex));
		for (const FImportAsyncHelper::FImportedAssetInfo& AssetInfo : AssetInfosPerSourceIndexPair.Value)
		{
			UObject* Asset = AssetInfo.ImportAsset;
			//In case Some factory code cannot run outside of the main thread we offer this callback to finish the work before calling post edit change (building the asset)
			if(bCallPostImportGameThreadCallback && AssetInfo.Factory)
			{
				UInterchangeFactoryBase::FPostImportGameThreadCallbackParams Arguments;
				Arguments.ImportedObject = Asset;
				Arguments.SourceData = AsyncHelper->SourceDatas[SourceIndex];
				Arguments.NodeUniqueID = AssetInfo.NodeUniqueId;
				Arguments.NodeContainer = AsyncHelper->BaseNodeContainers[SourceIndex].Get();
				AssetInfo.Factory->PostImportGameThreadCallback(Arguments);
			}

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
			if (AsyncHelper->TaskData.ReimportObject)
			{
				InterchangeManager->OnAssetPostReimport.Broadcast(Asset);
			}
			else
			{
				InterchangeManager->OnAssetPostImport.Broadcast(Asset);
				//Notify the asset registry, only when we have created the asset
				FAssetRegistryModule::AssetCreated(Asset);
			}

			AsyncHelper->AssetImportResult->AddImportedAsset(Asset);
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
