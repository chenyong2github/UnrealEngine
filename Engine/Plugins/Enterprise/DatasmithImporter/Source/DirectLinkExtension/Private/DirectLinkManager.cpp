// Copyright Epic Games, Inc. All Rights Reserved.

#include "DirectLinkManager.h"

#include "DirectLinkAssetObserver.h"
#include "DirectLinkExtensionSettings.h"
#include "DirectLinkExternalSource.h"
#include "DirectLinkUriResolver.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Async/Async.h"
#include "EditorReimportHandler.h"
#include "UObject/UObjectGlobals.h"

#if WITH_EDITOR
#include "Editor.h"
#endif //WITH_EDITOR

#define LOCTEXT_NAMESPACE "DirectLinkManager"

DEFINE_LOG_CATEGORY(LogDirectLinkManager);


namespace UE::DatasmithImporter
{
	FDirectLinkAutoReconnectManager::FDirectLinkAutoReconnectManager(FDirectLinkManager& InManager)
		: Manager(InManager)
		, bShouldRun(true)
	{
		if (const UDirectLinkExtensionSettings* DefaultSettings = GetDefault<UDirectLinkExtensionSettings>())
		{
			bAutoReconnectEnabled = DefaultSettings->bAutoReconnect;
			ReconnectionDelayInSeconds = DefaultSettings->ReconnectionDelayInSeconds;
		}
	}

	bool FDirectLinkAutoReconnectManager::Start()
	{
		if (bAutoReconnectEnabled && (!CompletedFuture.IsValid() || CompletedFuture.IsReady()))
		{
			bShouldRun = true;
			CompletedFuture = Async(EAsyncExecution::ThreadPool, [this]() {	Run(); });

			return true;
		}

		return false;
	}

	void FDirectLinkAutoReconnectManager::Stop()
	{
		bShouldRun = false;
	}

	void FDirectLinkAutoReconnectManager::Run()
	{
		const float CurrentTime = FPlatformTime::Seconds();
		const float TimeSinceLastTry = CurrentTime - LastTryTime;

		if (TimeSinceLastTry < ReconnectionDelayInSeconds)
		{
			FPlatformProcess::Sleep(ReconnectionDelayInSeconds - TimeSinceLastTry);
		}

		int32 NumberOfSources;
		{
			FWriteScopeLock ReconnectionScopeLock(Manager.ReconnectionListLock);
			for (int32 Index = Manager.ExternalSourcesToReconnect.Num() - 1; Index >= 0; --Index)
			{
				if (Manager.ExternalSourcesToReconnect[Index]->OpenStream())
				{
					Manager.ExternalSourcesToReconnect.RemoveAtSwap(Index);
				}
			}
			NumberOfSources = Manager.ExternalSourcesToReconnect.Num();
			LastTryTime = FPlatformTime::Seconds();
		}

		if (bShouldRun && NumberOfSources > 0)
		{
			// Could not reconnect, go back to the ThreadPool and try again later.
			CompletedFuture = Async(EAsyncExecution::ThreadPool, [this]() {	Run(); });
		}
	}

	/**
	 * #ueent_todo: The AutoReimport feature should be generalize to all FExternalSource, not just DirectLink ones.
	 */
	struct FAutoReimportInfo
	{
		FAutoReimportInfo(UObject* InTargetObject, const TSharedRef<FExternalSource>& InExternalSource, FDelegateHandle InImportDelegateHandle)
			: TargetObject(InTargetObject)
			, ExternalSource(InExternalSource)
			, ImportDelegateHandle(InImportDelegateHandle)
			, bChangedDuringPIE(false)
		{}

		TSoftObjectPtr<UObject> TargetObject;
		TSharedRef<FExternalSource> ExternalSource;
		FDelegateHandle ImportDelegateHandle;
		bool bChangedDuringPIE;
	};

	FDirectLinkManager::FDirectLinkManager()
		: Endpoint(MakeUnique<DirectLink::FEndpoint>(TEXT("UE5-Editor")))
		, AssetObserver(MakeUnique<FDirectLinkAssetObserver>(*this))
		, ReconnectionManager(MakeUnique<FDirectLinkAutoReconnectManager>(*this))
	{
		Endpoint->AddEndpointObserver(this);

#if WITH_EDITOR
		OnPIEEndHandle = FEditorDelegates::EndPIE.AddRaw(this, &FDirectLinkManager::OnEndPIE);
#endif //WITH_EDITOR
	}

	FDirectLinkManager::~FDirectLinkManager()
	{
		Endpoint->RemoveEndpointObserver(this);

		// Make sure all DirectLink external source become stales and their delegates stripped.
		for (const TPair<FSourceUri, TSharedRef<FDirectLinkExternalSource>>& UriExternalSourcePair : UriToExternalSourceMap)
		{
			UriExternalSourcePair.Value->Invalidate();
		}

#if WITH_EDITOR
		FEditorDelegates::EndPIE.Remove(OnPIEEndHandle);
#endif //WITH_EDITOR
	}

	TSharedPtr<FDirectLinkExternalSource> FDirectLinkManager::GetOrCreateExternalSource(const DirectLink::FSourceHandle& SourceHandle)
	{
		TSharedPtr<FDirectLinkExternalSource> DirectLinkExternalSource = nullptr;

		if (TSharedRef<FDirectLinkExternalSource>* ExternalSourceEntry = DirectLinkSourceToExternalSourceMap.Find(SourceHandle))
		{
			// A DirectLinkExternalSource already exists for this SourceHandle.
			DirectLinkExternalSource = *ExternalSourceEntry;
		}
		else if (RegisteredExternalSourcesInfo.Num() > 0)
		{
			FRWScopeLock ScopeLock(RawInfoLock, FRWScopeLockType::SLT_ReadOnly);
			DirectLink::FRawInfo::FDataPointInfo* SourceDataPointInfo = RawInfoCache.DataPointsInfo.Find(SourceHandle);
			FSourceUri ExternalSourceUri(GetUriFromSourceHandle(SourceHandle));

			if (SourceDataPointInfo && ExternalSourceUri.IsValid())
			{
				const FString& SourceName = SourceDataPointInfo->Name;
				const FString ExternalSourceName = FString::Printf(TEXT("%s_%s_ExternalSource"), *SourceName, *SourceHandle.ToString());
				const DirectLink::IConnectionRequestHandler::FSourceInformation SourceInfo{ SourceHandle };

				for (const FDirectLinkExternalSourceRegisterInformation& RegisteredInfo : RegisteredExternalSourcesInfo)
				{
					TSharedPtr<FDirectLinkExternalSource> UninitializedExternalSource = RegisteredInfo.SpawnFunction(ExternalSourceUri);

					if (UninitializedExternalSource->CanOpenNewConnection(SourceInfo))
					{
						DirectLinkExternalSource = UninitializedExternalSource;
						const FGuid DestinationHandle = Endpoint->AddDestination(ExternalSourceName, DirectLink::EVisibility::Private, DirectLinkExternalSource);
						DirectLinkExternalSource->Initialize(SourceName, SourceHandle, DestinationHandle);

						DirectLinkSourceToExternalSourceMap.Add(SourceHandle, DirectLinkExternalSource.ToSharedRef());
						UriToExternalSourceMap.Add(ExternalSourceUri, DirectLinkExternalSource.ToSharedRef());
						break;
					}
				}
			}
		}

		return DirectLinkExternalSource;
	}

	TSharedPtr<FDirectLinkExternalSource> FDirectLinkManager::GetOrCreateExternalSource(const FSourceUri& Uri)
	{
		DirectLink::FSourceHandle SourceHandle;
		TSharedPtr<FDirectLinkExternalSource> ExternalSource;

		if (TOptional<FDirectLinkSourceDescription> SourceDescription = FDirectLinkUriResolver::TryParseDirectLinkUri(Uri))
		{
			// Try getting the external source with the explicit id first.
			if (SourceDescription->SourceId)
			{
				SourceHandle = SourceDescription->SourceId.GetValue();
				if (SourceHandle.IsValid())
				{
					ExternalSource = GetOrCreateExternalSource(SourceHandle);
				}
			}
			
			// Could not retrieve the external source from the id, fall back on the first source matching the source description.
			if (!ExternalSource)
			{
				SourceHandle = ResolveSourceHandleFromDescription(SourceDescription.GetValue());
				if (SourceHandle.IsValid())
				{
					ExternalSource = GetOrCreateExternalSource(SourceHandle);
				}
			}
		}

		return ExternalSource;
	}

	DirectLink::FSourceHandle FDirectLinkManager::ResolveSourceHandleFromDescription(const FDirectLinkSourceDescription& SourceDescription) const
	{
		FRWScopeLock ScopeLock(RawInfoLock, FRWScopeLockType::SLT_ReadOnly);

		for (const auto& EndpointKeyValue : RawInfoCache.EndpointsInfo)
		{
			// Try to find a matching DirectLink source.
			if (EndpointKeyValue.Value.ComputerName == SourceDescription.ComputerName
				&& EndpointKeyValue.Value.ExecutableName == SourceDescription.ExecutableName
				&& EndpointKeyValue.Value.Name == SourceDescription.EndpointName)
			{
				for (const DirectLink::FRawInfo::FDataPointId& SourceInfo : EndpointKeyValue.Value.Sources)
				{
					if (SourceInfo.Name == SourceDescription.SourceName)
					{
						// Source found, returning the handle.
						return SourceInfo.Id;
					}
				}
			}
		}

		//Returning default invalid handle.
		return DirectLink::FSourceHandle();
	}

	void FDirectLinkManager::OnStateChanged(const DirectLink::FRawInfo& RawInfo)
	{
		{
			FRWScopeLock ScopeLock(RawInfoLock, FRWScopeLockType::SLT_Write);
			RawInfoCache = RawInfo;
		}

		UpdateSourceCache();

		CancelEmptySourcesLoading();
	}

	void FDirectLinkManager::UpdateSourceCache()
	{
		FRWScopeLock ScopeLock(RawInfoLock, FRWScopeLockType::SLT_ReadOnly);

		// List the source Id of all current external source. This is used to determine which ones are no longer valid.
		TSet<DirectLink::FSourceHandle> InvalidExternalSourceIds;
		DirectLinkSourceToExternalSourceMap.GetKeys(InvalidExternalSourceIds);

		for (const TPair<FMessageAddress, DirectLink::FRawInfo::FEndpointInfo>& EndpointInfoPair : RawInfoCache.EndpointsInfo)
		{
			if (!EndpointInfoPair.Value.bIsLocal)
			{
				continue;
			}

			for (const DirectLink::FRawInfo::FDataPointId& DataPointId : EndpointInfoPair.Value.Sources)
			{
				if (DataPointId.bIsPublic)
				{
					if (GetOrCreateExternalSource(DataPointId.Id))
					{
						// This source is still valid.
						InvalidExternalSourceIds.Remove(DataPointId.Id);
					}
				}
			}
		}


		TSet<DirectLink::FDestinationHandle> ActiveStreamsSources;
		for (const DirectLink::FRawInfo::FStreamInfo& StreamInfo : RawInfoCache.StreamsInfo)
		{
			if (!(StreamInfo.ConnectionState == DirectLink::EStreamConnectionState::Active
				|| StreamInfo.ConnectionState == DirectLink::EStreamConnectionState::RequestSent))
			{
				continue;
			}

			if (TSharedRef<FDirectLinkExternalSource>* ExternalSource = DirectLinkSourceToExternalSourceMap.Find(StreamInfo.Source))
			{
				ActiveStreamsSources.Add(StreamInfo.Source);
			}
		}

		{
			FWriteScopeLock ReconnectionScopeLock(ReconnectionListLock);

			for (int32 SourceIndex = ExternalSourcesToReconnect.Num() - 1; SourceIndex >= 0; --SourceIndex)
			{
				// If the source no longer exists, then there is no point in trying to reconnect.
				if (InvalidExternalSourceIds.Contains(ExternalSourcesToReconnect[SourceIndex]->GetSourceHandle()))
				{
					ExternalSourcesToReconnect.RemoveAtSwap(SourceIndex);
				}
			}

			for (const TPair<FGuid, TSharedRef<FDirectLinkExternalSource>>& ExternalSourceKeyValue : DirectLinkSourceToExternalSourceMap)
			{
				const TSharedRef<FDirectLinkExternalSource>& ExternalSource = ExternalSourceKeyValue.Value;

				if (ExternalSource->IsStreamOpen() && !ActiveStreamsSources.Contains(ExternalSourceKeyValue.Key))
				{
					// Lost connection, update the external source state and try to reconnect.
					ExternalSource->CloseStream();
					
					if (!ExternalSource->OpenStream())
					{
						// Could not reopen the stream, retry later.
						ExternalSourcesToReconnect.Add(ExternalSource);
						ReconnectionManager->Start();
					}
				}
			}
		}

		// Remove all external sources that are no longer valid.
		for (const DirectLink::FSourceHandle& SourceHandle : InvalidExternalSourceIds)
		{
			InvalidateSource(SourceHandle);
		}
	}

	void FDirectLinkManager::CancelEmptySourcesLoading() const
	{
		FRWScopeLock ScopeLock(RawInfoLock, FRWScopeLockType::SLT_ReadOnly);

		for (const DirectLink::FRawInfo::FStreamInfo& StreamInfo : RawInfoCache.StreamsInfo)
		{
			if (const TSharedRef<FDirectLinkExternalSource>* ExternalSourcePtr = DirectLinkSourceToExternalSourceMap.Find(StreamInfo.Source))
			{
				const TSharedRef<FDirectLinkExternalSource>& ExternalSource = *ExternalSourcePtr;
				
				// We can infer that a DirectLink source is empty (no scene synced) by looking at if its stream is planning to send any data.
				// #ueent_todo: Ideally it would be better to not allow an AsyncLoad to take place in the first time, but we can't know a source is empty 
				//				before actually connecting to it, so this is the best we can do at the current time.
				const bool bStreamIsEmpty = StreamInfo.ConnectionState == DirectLink::EStreamConnectionState::Active
					&& !StreamInfo.CommunicationStatus.IsTransmitting()
					&& StreamInfo.CommunicationStatus.TaskTotal == 0;

				if (bStreamIsEmpty
					&& ExternalSource->IsAsyncLoading()
					&& !ExternalSource->GetDatasmithScene().IsValid())
				{
					ExternalSource->CancelAsyncLoad();
					UE_LOG(LogDirectLinkManager, Warning, TEXT("The DirectLink source \"%s\" could not be loaded: Nothing to synchronize. Make sure to do a DirectLink sync in your exporter."), *ExternalSource->GetSourceName());
				}
			}
		}
	}

	void FDirectLinkManager::RegisterDirectLinkExternalSource(FDirectLinkExternalSourceRegisterInformation&& RegisterInformation)
	{
		RegisteredExternalSourcesInfo.Add(MoveTemp(RegisterInformation));
	}

	void FDirectLinkManager::UnregisterDirectLinkExternalSource(FName InName)
	{
		for (int32 InfoIndex = RegisteredExternalSourcesInfo.Num() - 1; InfoIndex >= 0; --InfoIndex)
		{
			if (RegisteredExternalSourcesInfo[InfoIndex].Name == InName)
			{
				RegisteredExternalSourcesInfo.RemoveAtSwap(InfoIndex);
				break;
			}
		}
	}

	DirectLink::FEndpoint& FDirectLinkManager::GetEndpoint()
	{
		return *Endpoint.Get();
	}

	void FDirectLinkManager::InvalidateSource(const DirectLink::FSourceHandle& InvalidSourceHandle)
	{
		TSharedRef<FDirectLinkExternalSource> DirectLinkExternalSource = DirectLinkSourceToExternalSourceMap.FindAndRemoveChecked(InvalidSourceHandle);
		UriToExternalSourceMap.Remove(DirectLinkExternalSource->GetSourceUri());

		// Clear the auto-reimport cache for this external source.
		TArray<TSharedRef<FAutoReimportInfo>> AutoReimportInfoList;
		RegisteredAutoReimportExternalSourceMap.MultiFind(DirectLinkExternalSource, AutoReimportInfoList);
		RegisteredAutoReimportExternalSourceMap.Remove(DirectLinkExternalSource);
		for (const TSharedRef<FAutoReimportInfo>& AutoReimportInfo : AutoReimportInfoList)
		{
			RegisteredAutoReimportObjectMap.Remove(AutoReimportInfo->TargetObject.Get());
		}

		DirectLinkExternalSource->Invalidate();
	}

	bool FDirectLinkManager::IsAssetAutoReimportEnabled(UObject* InAsset) const
	{
		return RegisteredAutoReimportObjectMap.Find(InAsset) != nullptr;
	}

	bool FDirectLinkManager::SetAssetAutoReimport(UObject* InAsset, bool bEnabled)
	{
		return bEnabled ? EnableAssetAutoReimport(InAsset) : DisableAssetAutoReimport(InAsset);
	}

	bool FDirectLinkManager::EnableAssetAutoReimport(UObject* InAsset)
	{	
		// Enable auto-reimport for InAsset.
		FAssetData AssetData(InAsset);
		const FSourceUri Uri = FSourceUri::FromAssetData(AssetData);
		const bool bIsValidDirectLinkUri = Uri.IsValid() && Uri.HasScheme(FDirectLinkUriResolver::GetDirectLinkScheme());

		if (bIsValidDirectLinkUri && !RegisteredAutoReimportObjectMap.Contains(InAsset))
		{
			if (TSharedPtr<FDirectLinkExternalSource> ExternalSource = GetOrCreateExternalSource(Uri))
			{
				// Register a delegate triggering a reimport task on the external source snapshotupdate event.
				// That way the asset will be auto-reimported and kept up-to-date.
				FDelegateHandle DelegateHandle = ExternalSource->OnExternalSourceChanged.AddRaw(this, &FDirectLinkManager::OnExternalSourceChanged);

				TSharedRef<FDirectLinkExternalSource> ExternalSourceRef = ExternalSource.ToSharedRef();
				TSharedRef<FAutoReimportInfo> AutoReimportInfo = MakeShared<FAutoReimportInfo>(InAsset, ExternalSourceRef, DelegateHandle);

				RegisteredAutoReimportObjectMap.Add(InAsset, AutoReimportInfo);
				RegisteredAutoReimportExternalSourceMap.Add(ExternalSourceRef, AutoReimportInfo);
				ExternalSource->OpenStream();

				return true;
			}
		}

		return false;
	}

	bool FDirectLinkManager::DisableAssetAutoReimport(UObject* InAsset)
	{
		// Disable auto-reimport for InAsset.
		if (const TSharedRef<FAutoReimportInfo>* AutoReimportInfoPtr = RegisteredAutoReimportObjectMap.Find(InAsset))
		{
			// Holding a local reference to the FAutoReimportInfo to ensure its lifetime while we are cleaning up.
			const TSharedRef<FAutoReimportInfo> AutoReimportInfo(*AutoReimportInfoPtr);

			AutoReimportInfo->ExternalSource->OnExternalSourceChanged.Remove(AutoReimportInfo->ImportDelegateHandle);
			RegisteredAutoReimportObjectMap.Remove(InAsset);
			RegisteredAutoReimportExternalSourceMap.RemoveSingle(AutoReimportInfo->ExternalSource, AutoReimportInfo);
			return true;
		}

		return false;
	}

	void FDirectLinkManager::UpdateModifiedRegisteredAsset(UObject* InAsset)
	{
		if (!IsAssetAutoReimportEnabled(InAsset))
		{
			// Asset is not registered, nothing to update.
			return;
		}

		const FAssetData AssetData(InAsset);
		const FSourceUri Uri = FSourceUri::FromAssetData(AssetData);
		const bool bIsDirectLinkUri = Uri.IsValid() && Uri.HasScheme(FDirectLinkUriResolver::GetDirectLinkScheme());
		const TSharedPtr<FExternalSource> UpdatedExternalSource = bIsDirectLinkUri ? GetOrCreateExternalSource(Uri) : nullptr;
		if (!UpdatedExternalSource)
		{
			// Asset was registered for auto reimport but no longer has a DirectLink source, disable auto reimport.
			DisableAssetAutoReimport(InAsset);
			return;
		}

		const bool bHasDirectLinkSourceChanged = RegisteredAutoReimportObjectMap.FindChecked(InAsset)->ExternalSource != UpdatedExternalSource;
		if (bHasDirectLinkSourceChanged)
		{
			// The source changed but is still a DirectLink source.
			// Since the auto-reimport is asset-driven and not source-driven, keep the auto reimport active with the new source.	
			DisableAssetAutoReimport(InAsset);
			EnableAssetAutoReimport(InAsset);
		}
	}

	TArray<TSharedRef<FDirectLinkExternalSource>> FDirectLinkManager::GetExternalSourceList() const
	{
		TArray<TSharedRef<FDirectLinkExternalSource>> ExternalSources;
		UriToExternalSourceMap.GenerateValueArray(ExternalSources);
		return ExternalSources;
	}

	FSourceUri FDirectLinkManager::GetUriFromSourceHandle(const DirectLink::FSourceHandle& SourceHandle)
	{
		FRWScopeLock ScopeLock(RawInfoLock, FRWScopeLockType::SLT_ReadOnly);

		if (const DirectLink::FRawInfo::FDataPointInfo* SourceInfo = RawInfoCache.DataPointsInfo.Find(SourceHandle))
		{
			const FString SourceName(SourceInfo->Name);
			if (DirectLink::FRawInfo::FEndpointInfo* EndpointInfo = RawInfoCache.EndpointsInfo.Find(SourceInfo->EndpointAddress))
			{
				const FString EndpointName(EndpointInfo->Name);
				const FString UriPath(EndpointInfo->ComputerName / EndpointInfo->ExecutableName / EndpointInfo->Name / SourceName);
				TMap<FString, FString> UriQuery = { {FDirectLinkUriResolver::GetSourceIdPropertyName(), LexToString(SourceHandle)} };

				return FSourceUri(FDirectLinkUriResolver::GetDirectLinkScheme(), UriPath, UriQuery);
			}
		}

		return FSourceUri();
	}

	void FDirectLinkManager::OnExternalSourceChanged(const TSharedRef<FExternalSource>& ExternalSource)
	{
		// Accumulate the reimport request in a thread-safe queue that will be processed in the main thread.
		// Multiple reimport request for the same external source will only be processed once. 
		// Doing this allows us to skip redundant reimports, as the reimport already uses the latest data from the ExternalSource.
		PendingReimportQueue.Enqueue(ExternalSource);

		Async(EAsyncExecution::TaskGraphMainThread, [this]() {

			TSet<TSharedRef<FExternalSource>> PendingReimportSet;
			TSharedPtr<FExternalSource> EnqueuedSourceToReimport;
			while (PendingReimportQueue.Dequeue(EnqueuedSourceToReimport))
			{
				PendingReimportSet.Add(EnqueuedSourceToReimport.ToSharedRef());
			}

			for (const TSharedRef<FExternalSource>& ExternalSourceToReimport : PendingReimportSet)
			{
				TriggerAutoReimportOnExternalSource(ExternalSourceToReimport);
			}
		});
	}

	void FDirectLinkManager::TriggerAutoReimportOnExternalSource(const TSharedRef<FExternalSource>& ExternalSource)
	{
		TArray<TSharedRef<FAutoReimportInfo>> AutoReimportInfos;
		RegisteredAutoReimportExternalSourceMap.MultiFind(ExternalSource, AutoReimportInfos);
		if (AutoReimportInfos.Num() == 0)
		{
			return;
		}

		for (const TSharedRef<FAutoReimportInfo>& AutoReimportInfo : AutoReimportInfos)
		{
#if WITH_EDITOR
			// If we're in PIE, delay the callbacks until we exit that mode.
			if (GIsEditor && FApp::IsGame())
			{
				AutoReimportInfo->bChangedDuringPIE = true;
				UE_LOG(LogDirectLinkManager, Warning, TEXT("The DirectLink source \"%s\" received an update while in PIE mode. The reimport will be triggered when exiting PIE."), *ExternalSource->GetSourceName());
				continue;
			}
#endif //WITH_EDITOR

			if (UObject* Asset = AutoReimportInfo->TargetObject.Get())
			{
				TriggerAutoReimportOnAsset(AutoReimportInfo->TargetObject.Get());
			}
		}
	}

	void FDirectLinkManager::TriggerAutoReimportOnAsset(UObject* Asset)
	{
		const FAssetData AssetData(Asset);
		const FSourceUri Uri = FSourceUri::FromAssetData(AssetData);
		const bool bIsStillValidDirectLinkUri = Uri.IsValid() && Uri.HasScheme(FDirectLinkUriResolver::GetDirectLinkScheme());

		// Make sure we are not triggering a reimport on an asset that doesn't have a DirectLink source.
		if (bIsStillValidDirectLinkUri)
		{
			FReimportManager::Instance()->Reimport(Asset, /*bAskForNewFileIfMissing*/ false, /*bShowNotification*/ true, /*PreferredReimportFile*/ TEXT(""), /*SpecifiedReimportHandler */ nullptr, /*SourceFileIndex*/ INDEX_NONE, /*bForceNewFile*/ false, /*bAutomated*/ true);
		}
		else
		{
			DisableAssetAutoReimport(Asset);
		}
	}

#if WITH_EDITOR
	void FDirectLinkManager::OnEndPIE(bool bIsSimulating)
	{
		TArray<UObject*> AssetsToReimport;
		TArray<UObject*> InvalidAssets;

		// We can't call TriggerOnExternalSourceChanged() directly as it may remove items in RegisteredAutoReimportObjectMap while we iterate.
		for (TPair<UObject*, TSharedRef<FAutoReimportInfo>>& AutoReimportEntry : RegisteredAutoReimportObjectMap)
		{
			if (AutoReimportEntry.Value->TargetObject.IsValid())
			{
				if (AutoReimportEntry.Value->bChangedDuringPIE)
				{
					AutoReimportEntry.Value->bChangedDuringPIE = false;
					AssetsToReimport.Add(AutoReimportEntry.Key);
				}
			}
			else
			{
				InvalidAssets.Add(AutoReimportEntry.Key);
			}
		}

		for (UObject* CurrentAsset : AssetsToReimport)
		{
			// Trigger the event held off during PIE.
			TriggerAutoReimportOnAsset(CurrentAsset);
		}

		for (UObject* CurrentAsset : InvalidAssets)
		{
			DisableAssetAutoReimport(CurrentAsset);
		}
	}
#endif //WITH_EDITOR
}

#undef LOCTEXT_NAMESPACE
