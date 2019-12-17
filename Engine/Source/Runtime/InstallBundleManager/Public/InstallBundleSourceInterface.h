// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InstallBundleTypes.h"
#include "InstallBundleUtils.h"

class IInstallBundleSource;
class IAnalyticsProviderET;

DECLARE_DELEGATE_TwoParams(FInstallBundleSourceInitDelegate, TSharedRef<IInstallBundleSource> /*Source*/, FInstallBundleSourceInitInfo /*InitInfo*/);

DECLARE_DELEGATE_OneParam(FInstallBundleCompleteDelegate, FInstallBundleSourceRequestResultInfo /*Result*/);
DECLARE_DELEGATE_OneParam(FInstallBundlePausedDelegate, FInstallBundleSourcePauseInfo /*PauseInfo*/);

class IInstallBundleSource : public TSharedFromThis<IInstallBundleSource>
{
public:
	virtual ~IInstallBundleSource() {}

	// Returns a unique id for this source
	virtual EInstallBundleSourceType GetSourceType() const = 0;

	// Returns the how this source should be weighted when combined with other sources
	virtual float GetSourceWeight() const { return 1.0f; }

	// Returns true if this source can remove content on init
	virtual bool SupportsRemoveContentOnNextInit() const { return false; }

	// Returns true if this source can cancel requests
	virtual bool SupportsCanceling() const { return false; }

	// Returns true if this source can pause requests
	virtual bool SupportsPausing() const { return false; }

	// Called once by bundle manager after constructing the bundle source
	virtual void Init(
		TSharedRef<FConfigFile> InstallBundleConfig, 
		TSharedRef<InstallBundleUtil::FContentRequestStatsMap> InRequestStats,
		TSharedPtr<IAnalyticsProviderET> AnalyticsProvider) = 0;
	// Bundle manager will not call AsyncInit again until the bundle source calls back that it is complete
	// It will be retried indefinitely until init is successful.  
	virtual void AsyncInit(FInstallBundleSourceInitDelegate Callback) = 0;

	// Whether this source has been initialized or not
	virtual EInstallBundleManagerInitState GetInitState() const = 0;

	// Returns content version in a "<BuildVersion>-<Platform>" format
	virtual FString GetContentVersion() const = 0;

	// Gets the state of content on disk
	// BundleNames contains all dependencies and has been deduped
	virtual void GetContentState(TArrayView<FName> BundleNames, EInstallBundleGetContentStateFlags Flags, FInstallBundleGetContentStateDelegate Callback) = 0;

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

	// Returns true if content is scheduled to be removed the next time the source is initialized
	// BundleNames contains all dependencies and has been deduped
	virtual bool RequestRemoveContentOnNextInit(TArrayView<FName> RemoveNames) { return false; }
	// Call to cancel the removal of any content scheduled for removal the next time the source is initialized
	// Returns true if all bundles were canceled
	virtual bool CancelRequestRemoveContentOnNextInit(TArrayView<FName> BundleNames) { return false;  }

	// Cancel the install for the specified bundles
	// BundleNames contains all dependencies and has been deduped
	virtual void CancelBundles(TArrayView<FName> BundleNames, EInstallBundleCancelFlags Flags) {}

	// Pause/Resume bundles.  Returns true if any bundles were paused/resumed
	virtual bool PauseBundles(TArrayView<FName> BundleNames) { return false; }
	virtual bool ResumeBundles(TArrayView<FName> BundleNames) { return false; }

	// Called by bundle manager to update pause status.  Returns a struct indicating current pause status and if that status has changed.
	virtual FInstallBundleSourcePauseInfo UpdateBundlePauseInfo(FName BundleName) 
	{ 
		FInstallBundleSourcePauseInfo Info;
		Info.BundleName = BundleName;
		return Info;
	}

	// TODO: UpdateContentRequestFlags - Probably need to rework the whole thing.  The only flag it makes sense for is CheckForCellularDataUsage

	// TODO: GetBundleProgress() - Not sure how this will work yet

	// Called by bundle manager to pass through command line options to simulate errors
	virtual void SetErrorSimulationCommands(const FString& CommandLine) {}
};
