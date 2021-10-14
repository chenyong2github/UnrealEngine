// Copyright Epic Games, Inc. All Rights Reserved.

#include "DirectLinkManager.h"

#include "DirectLinkUriResolver.h"
#include "DirectLinkExternalSource.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Async/Async.h"
#include "EditorReimportHandler.h"
#include "UObject/UObjectGlobals.h"


#define LOCTEXT_NAMESPACE "DirectLinkManager"


namespace UE::DatasmithImporter
{
	struct FAutoReimportInfo
	{
		FAutoReimportInfo(UObject* InTargetObject, const TSharedRef<FDirectLinkExternalSource>& InExternalSource, FDelegateHandle InImportDelegateHandle)
			: TargetObject(InTargetObject)
			, ExternalSource(InExternalSource)
			, ImportDelegateHandle(InImportDelegateHandle)
		{}

		TSoftObjectPtr<UObject> TargetObject;
		TSharedRef<FDirectLinkExternalSource> ExternalSource;
		FDelegateHandle ImportDelegateHandle;
	};

	FDirectLinkManager::FDirectLinkManager()
		: Endpoint(MakeUnique<DirectLink::FEndpoint>(TEXT("UE5-Editor")))
	{
		Endpoint->AddEndpointObserver(this);
	}

	FDirectLinkManager::~FDirectLinkManager()
	{
		Endpoint->RemoveEndpointObserver(this);
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
		DirectLink::FSourceHandle SourceHandle = GetSourceHandleFromUri(Uri);

		if (SourceHandle.IsValid())
		{
			return GetOrCreateExternalSource(SourceHandle);
		}

		return nullptr;
	}

	DirectLink::FSourceHandle FDirectLinkManager::GetSourceHandleFromUri(const FSourceUri& Uri) const
	{
		if (TOptional<FDirectLinkSourceDescription> SourceDescription = FDirectLinkUriResolver::TryParseDirectLinkUri(Uri))
		{
			FRWScopeLock ScopeLock(RawInfoLock, FRWScopeLockType::SLT_ReadOnly);

			for (const auto& EndpointKeyValue : RawInfoCache.EndpointsInfo)
			{
				// Try to find a matching DirectLink source.
				if (EndpointKeyValue.Value.ComputerName == SourceDescription->ComputerName
					&& EndpointKeyValue.Value.ExecutableName == SourceDescription->ExecutableName
					&& EndpointKeyValue.Value.Name == SourceDescription->EndpointName)
				{
					for (const DirectLink::FRawInfo::FDataPointId& SourceInfo : EndpointKeyValue.Value.Sources)
					{
						if (SourceInfo.Name == SourceDescription->SourceName)
						{
							// Source found, returning the handle.
							return SourceInfo.Id;
						}
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

		// Remove all external sources that are no longer valid.
		for (const DirectLink::FSourceHandle& SourceHandle : InvalidExternalSourceIds)
		{
			InvalidateSource(SourceHandle);
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


	bool FDirectLinkManager::SetAssetAutoReimport(UObject* InAsset, bool bEnableAutoReimport)
	{
		if (!bEnableAutoReimport)
		{
			// Disable auto-reimport for InAsset.
			if (const TSharedRef<FAutoReimportInfo>* AutoReimportInfoPtr = RegisteredAutoReimportObjectMap.Find(InAsset))
			{
				// Holding a local reference to the FAutoReimportInfo to ensure its lifetime while we are cleaning up.
				const TSharedRef<FAutoReimportInfo> AutoReimportInfo(*AutoReimportInfoPtr);

				AutoReimportInfo->ExternalSource->OnExternalSourceLoaded.Remove(AutoReimportInfo->ImportDelegateHandle);
				RegisteredAutoReimportObjectMap.Remove(InAsset);
				RegisteredAutoReimportExternalSourceMap.RemoveSingle(AutoReimportInfo->ExternalSource, AutoReimportInfo);
				return true;
			}
		}
		else
		{
			// Enable auto-reimport for InAsset.
			FSourceUri Uri;
			const FAssetData AssetData(InAsset);
			const bool bIsValidDirectLinkUri = GetAssetSourceUri(AssetData, Uri)
				&& Uri.HasScheme(FDirectLinkUriResolver::GetDirectLinkScheme());

			if (bIsValidDirectLinkUri && !RegisteredAutoReimportObjectMap.Contains(InAsset))
			{
				if (TSharedRef<FDirectLinkExternalSource>* ExternalSource = UriToExternalSourceMap.Find(Uri))
				{
					// Register a delegate triggering a reimport task on the external source snapshotupdate event.
					// That way the asset will be auto-reimported and kept up-to-date.
					FDelegateHandle DelegateHandle = (*ExternalSource)->OnExternalSourceLoaded.AddLambda([AssetData](const TSharedRef<FExternalSource>& ExternalSource) {				
						UObject* AssetToReimport = AssetData.GetAsset();
						FReimportManager::Instance()->Reimport(AssetToReimport, /*bAskForNewFileIfMissing*/ false, /*bShowNotification*/ true, /*PreferredReimportFile*/ TEXT(""), /*SpecifiedReimportHandler */ nullptr, /*SourceFileIndex*/ INDEX_NONE, /*bForceNewFile*/ false, /*bAutomated*/ true);
					});

					TSharedRef<FAutoReimportInfo> AutoReimportInfo = MakeShared<FAutoReimportInfo>(InAsset, *ExternalSource, DelegateHandle);

					RegisteredAutoReimportObjectMap.Add(InAsset, AutoReimportInfo);
					RegisteredAutoReimportExternalSourceMap.Add(*ExternalSource, AutoReimportInfo);

					return true;
				}
			}
		}

		return false;
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

				return FSourceUri(FDirectLinkUriResolver::GetDirectLinkScheme(), UriPath);
			}
		}

		return FSourceUri();
	}

	bool FDirectLinkManager::GetAssetSourceUri(const FAssetData& AssetData, FSourceUri& OutUri) const
	{
		const FName SourceUriTagName("SourceUri");
		const FAssetTagValueRef ValueRef = AssetData.TagsAndValues.FindTag(SourceUriTagName);

		if (ValueRef.IsSet())
		{
			OutUri = FSourceUri(ValueRef.GetValue());
			return OutUri.IsValid();
		}

		return false;
	}
}

#undef LOCTEXT_NAMESPACE
