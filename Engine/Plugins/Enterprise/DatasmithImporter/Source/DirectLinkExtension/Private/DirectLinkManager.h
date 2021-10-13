// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDirectLinkManager.h"
#include "DirectLinkExtensionModule.h"

#include "SourceUri.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Delegates/DelegateCombinations.h"
#include "DirectLinkEndpoint.h"
#include "HAL/CriticalSection.h"
#include "IUriResolver.h"
#include "ObjectTools.h"

struct FAssetData;

namespace UE::DatasmithImporter
{
	class FDirectLinkExternalSource;
	struct FAutoReimportInfo;

	class FDirectLinkManager: public IDirectLinkManager, public DirectLink::IEndpointObserver
	{
	public:
		FDirectLinkManager();
		virtual ~FDirectLinkManager();

		// IEndpointObserver interface begin
		virtual void OnStateChanged(const DirectLink::FRawInfo& RawInfo) override;
		// IEndpointObserver interface end

		// IDirectLinkManager interface begin
		virtual TSharedPtr<FDirectLinkExternalSource> GetOrCreateExternalSource(const DirectLink::FSourceHandle& SourceHandle) override;
		virtual TSharedPtr<FDirectLinkExternalSource> GetOrCreateExternalSource(const FSourceUri& Uri) override;
		virtual DirectLink::FEndpoint& GetEndpoint() override;
		virtual FSourceUri GetUriFromSourceHandle(const DirectLink::FSourceHandle& SourceHandle) override;
		virtual bool SetAssetAutoReimport(UObject* InAsset, bool bEnableAutoReimport) override;
		virtual TArray<TSharedRef<FDirectLinkExternalSource>> GetExternalSourceList() const override;
		virtual void UnregisterDirectLinkExternalSource(FName InName) override;
	protected:
		virtual void RegisterDirectLinkExternalSource(FDirectLinkExternalSourceRegisterInformation&& ExternalSourceClass) override;
		// IDirectLinkManager interface end

	private:
		/**
		 * Try to extract the SourceUri from the AssetData tags.
		 * @param AssetData	The asset data.
		 * @param OutUri	Out parameter containing the resulting SourceUri
		 * @return True if the FSourceUri was successfully extracted.
		 */
		bool GetAssetSourceUri(const FAssetData& AssetData, FSourceUri& OutUri) const;
		
		/**
		 * Remove a DirectLink source from cache and invalidate its associated DirectLinkExternalSource object.
		 * @param InvalidSourceId	The SourceHandle of the invalid DirectLink source.
		 */
		void InvalidateSource(const DirectLink::FSourceHandle& InvalidSourceHandle);

		/**
		 * Try to get a DirectLink SourceHandle from a SourceUri.
		 * @param Uri	An Uri pointing to a DirectLink source.
		 * @return	The DirectLink SourceHandle corresponding to the Uri. If no match was found the FSourceHandle is invalid.
		 */
		DirectLink::FSourceHandle GetSourceHandleFromUri(const FSourceUri& Uri) const;

		/**
		 * Update internal cache. Create FDirectLinkExternalSource for new DirectLink source and remove expired ones.
		 */
		void UpdateSourceCache();

	private:
		/**
		 * Cached DirectLink state.
		 */
		DirectLink::FRawInfo RawInfoCache;

		/**
		 * Lock used to guard RawInfoCache, as the cache is updated from an async thread.
		 */
		mutable FRWLock RawInfoLock;

		TUniquePtr<DirectLink::FEndpoint> Endpoint;

		TArray<FDirectLinkExternalSourceRegisterInformation> RegisteredExternalSourcesInfo;
		
		TMap<FSourceUri, TSharedRef<FDirectLinkExternalSource>> UriToExternalSourceMap;
		
		TMap<DirectLink::FSourceHandle, TSharedRef<FDirectLinkExternalSource>> DirectLinkSourceToExternalSourceMap;

		TMap<UObject*, TSharedRef<FAutoReimportInfo>> RegisteredAutoReimportObjectMap;
		
		TMultiMap<TSharedRef<FDirectLinkExternalSource>, TSharedRef<FAutoReimportInfo>> RegisteredAutoReimportExternalSourceMap;
	};
}