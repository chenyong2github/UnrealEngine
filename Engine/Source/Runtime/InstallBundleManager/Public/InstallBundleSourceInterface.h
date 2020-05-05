// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InstallBundleTypes.h"
#include "InstallBundleUtils.h"

class IInstallBundleSource;
class IAnalyticsProviderET;

DECLARE_DELEGATE_TwoParams(FInstallBundleSourceInitDelegate, TSharedRef<IInstallBundleSource> /*Source*/, FInstallBundleSourceAsyncInitInfo /*InitInfo*/);

DECLARE_DELEGATE_TwoParams(FInstallBundleSourceQueryBundleInfoDelegate, TSharedRef<IInstallBundleSource> /*Source*/, FInstallBundleSourceBundleInfoQueryResultInfo /*Result*/);
DECLARE_DELEGATE_RetVal_TwoParams(EInstallBundleSourceUpdateBundleInfoResult, FInstallBundleSourceUpdateBundleInfoDelegate, TSharedRef<IInstallBundleSource> /*Source*/, FInstallBundleSourceBundleInfoQueryResultInfo /*Result*/);

DECLARE_DELEGATE_OneParam(FInstallBundleCompleteDelegate, FInstallBundleSourceUpdateContentResultInfo /*Result*/);
DECLARE_DELEGATE_OneParam(FInstallBundlePausedDelegate, FInstallBundleSourcePauseInfo /*PauseInfo*/);
DECLARE_DELEGATE_OneParam(FInstallBundleRemovedDelegate, FInstallBundleSourceRemoveContentResultInfo /*Result*/);

DECLARE_DELEGATE_TwoParams(FInstallBundleSourceContentPatchResultDelegate, TSharedRef<IInstallBundleSource> /*Source*/, bool /*bContentPatchRequired*/);

class IInstallBundleSource : public TSharedFromThis<IInstallBundleSource>
{
public:
	virtual ~IInstallBundleSource() {}

	// Returns a unique id for this source
	virtual EInstallBundleSourceType GetSourceType() const = 0;

	// Returns the how this source should be weighted when combined with other sources
	virtual float GetSourceWeight() const { return 1.0f; }

	// Called once by bundle manager after constructing the bundle source
	// Any non-fallback errors returned will cause bundle manager to fail to initialize
	virtual FInstallBundleSourceInitInfo Init(
		TSharedRef<InstallBundleUtil::FContentRequestStatsMap> InRequestStats,
		TSharedPtr<IAnalyticsProviderET> AnalyticsProvider,
		TSharedPtr<InstallBundleUtil::PersistentStats::FPersistentStatContainerBase> PersistentStatsContainer) = 0;
	// Bundle manager will not call AsyncInit again until the bundle source calls back that it is complete
	// It will be retried indefinitely until init is successful.  
	virtual void AsyncInit(FInstallBundleSourceInitDelegate Callback) = 0;

	// Currently only called after AsyncInit initialization.
	// Provides information about bundles this source knows about back to bundle manager.
	virtual void AsyncInit_QueryBundleInfo(FInstallBundleSourceQueryBundleInfoDelegate OnCompleteCallback) = 0;

	virtual void AsyncInit_SetUpdateBundleInfoCallback(FInstallBundleSourceUpdateBundleInfoDelegate UpdateCallback) {}

	// Whether this source has been initialized or not
	virtual EInstallBundleManagerInitState GetInitState() const = 0;

	// Returns content version in a "<BuildVersion>-<Platform>" format
	virtual FString GetContentVersion() const = 0;

	// Finds all dependencies for InBundleName, including InBundleName
	// Sets bSkippedUnknownBundles if information for InBundleName or a dependency can't be found
	virtual TSet<FName> GetBundleDependencies(FName InBundleName, bool* bSkippedUnknownBundles /*= nullptr*/) const = 0;

	// Gets the state of content on disk
	// BundleNames contains all dependencies and has been deduped
	virtual void GetContentState(TArrayView<const FName> BundleNames, EInstallBundleGetContentStateFlags Flags, FInstallBundleGetContentStateDelegate Callback) = 0;

	// Allows this bundle source to reject bundle requests early, rather than failing them.
	// This means that client code does not have to wait on these bundles.
	// This may be called anytime after Init, even during AsyncInit
	virtual EInstallBundleSourceBundleSkipReason GetBundleSkipReason(FName BundleName) const { return EInstallBundleSourceBundleSkipReason::None; }

	struct FRequestUpdateContentBundleContext
	{
		FName BundleName;
		EInstallBundleRequestFlags Flags = EInstallBundleRequestFlags::None;
		FInstallBundlePausedDelegate PausedCallback;
		FInstallBundleCompleteDelegate CompleteCallback;
		TSharedPtr<InstallBundleUtil::FContentRequestSharedContext> RequestSharedContext;
	};

	// Updates content on disk if necessary
	// BundleContexts contains all dependencies and has been deduped
	virtual void RequestUpdateContent(FRequestUpdateContentBundleContext BundleContext) = 0;

	struct FRequestRemoveContentBundleContext
	{
		FName BundleName;
		FInstallBundleRemovedDelegate CompleteCallback;
	};

	// Removes content from disk if present
	// BundleContexts contains all dependencies and has been deduped
	// Bundle manager will not schedule removes at the same time as updates for the same bundle
	virtual void RequestRemoveContent(FRequestRemoveContentBundleContext BundleContext)
	{
		FInstallBundleSourceRemoveContentResultInfo ResultInfo;
		ResultInfo.BundleName = BundleContext.BundleName;
		BundleContext.CompleteCallback.Execute(MoveTemp(ResultInfo));
	}

	// Returns true if content is scheduled to be removed the next time the source is initialized
	// BundleNames contains all dependencies and has been deduped
	virtual bool RequestRemoveContentOnNextInit(TArrayView<const FName> RemoveNames) { return false; }
	// Call to cancel the removal of any content scheduled for removal the next time the source is initialized
	// Returns true if all bundles were canceled
	virtual bool CancelRequestRemoveContentOnNextInit(TArrayView<const FName> BundleNames) { return false; }

	// Cancel the install for the specified bundles
	virtual void CancelBundles(TArrayView<const FName> BundleNames, EInstallBundleCancelFlags Flags) {}

	// User Pause/Resume bundles.
	virtual void UserPauseBundles(TArrayView<const FName> BundleNames) {}
	virtual void UserResumeBundles(TArrayView<const FName> BundleNames) {}

	// UpdateContentRequestFlags - Allow some flags to be updated for in flight requests
	// Currently only CheckForCellularDataUsage is supported
	virtual EInstallBundleRequestFlags GetModifyableContentRequestFlags() const { return EInstallBundleRequestFlags::None; }
	virtual void UpdateContentRequestFlags(TArrayView<const FName> BundleNames, EInstallBundleRequestFlags AddFlags, EInstallBundleRequestFlags RemoveFlags) {}

	// Derived classes should implement this if their content install will take a significant amount of time
	virtual TOptional<FInstallBundleSourceProgress> GetBundleProgress(FName BundleName) const { return TOptional<FInstallBundleSourceProgress>(); }

	virtual void CheckForContentPatch(FInstallBundleSourceContentPatchResultDelegate Callback) { Callback.Execute(AsShared(), false); }

	// Called by bundle manager to pass through command line options to simulate errors
	virtual void SetErrorSimulationCommands(const FString& CommandLine) {}
};
