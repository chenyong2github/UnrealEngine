// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Misc/EnumClassFlags.h"
#include "Misc/ConfigCacheIni.h"
#include "Logging/LogMacros.h"
#include "Internationalization/Text.h"

enum class EInstallBundleResult : int
{
	OK,
	FailedPrereqRequiresLatestClient,
	InstallError,
	InstallerOutOfDiskSpaceError,
	OnCellularNetworkError,
	NoInternetConnectionError,
	UserCancelledError,
	InitializationError,
	Count,
};

inline const TCHAR* GetInstallBundleResultString(EInstallBundleResult Result)
{
	using UnderType = __underlying_type(EInstallBundleResult);
	static const TCHAR* Strings[] =
	{
		TEXT("OK"),
		TEXT("FailedPrereqRequiresLatestClient"),
		TEXT("InstallError"),
		TEXT("InstallerOutOfDiskSpaceError"),
		TEXT("OnCellularNetworkError"),
		TEXT("NoInternetConnectionError"),
		TEXT("UserCancelledError"),
		TEXT("InitializationError"),
	};
	static_assert(static_cast<UnderType>(EInstallBundleResult::Count) == ARRAY_COUNT(Strings), "");

	return Strings[static_cast<UnderType>(Result)];
}

enum class EInstallBundleModuleInitResult : int
{
	OK,
	BuildMetaDataNotFound,
	BuildMetaDataParsingError,
	DistributionRootParseError,
	DistributionRootDownloadError,
	ManifestCreationError,
	ManifestDownloadError,
	BackgroundDownloadsIniDownloadError,
	NoInternetConnectionError,
	Count
};

inline const TCHAR* GetInstallBundleModuleInitResultString(EInstallBundleModuleInitResult Result)
{
	using UnderType = __underlying_type(EInstallBundleModuleInitResult);
	static const TCHAR* Strings[] =
	{
		TEXT("OK"),
		TEXT("BuildMetaDataNotFound"),
		TEXT("BuildMetaDataParsingError"),
		TEXT("DistributionRootParseError"),
		TEXT("DistributionRootDownloadError"),
		TEXT("ManifestCreationError"),
		TEXT("ManifestDownloadError"),
		TEXT("BackgroundDownloadsIniDownloadError"),
		TEXT("NoInternetConnectionError"),
	};
	static_assert(static_cast<UnderType>(EInstallBundleModuleInitResult::Count) == ARRAY_COUNT(Strings), "");

	return Strings[static_cast<UnderType>(Result)];
}

enum class EInstallBundleRequestFlags : uint32
{
	None = 0,
	CheckForCellularDataUsage = (1 << 0),
};
ENUM_CLASS_FLAGS(EInstallBundleRequestFlags)

enum class EInstallBundleStatus : int
{
	NotRequested,
	RequestedQueued,
	Downloading,
	Installing,
	Finishing,
	Installed,
};

struct FInstallBundleProgress
{
	// Num bytes received
	uint64 ProgressBytes = 0;
	// Num bytes written to storage (<= ProgressBytes)
	uint64 ProgressBytesWritten = 0;
	// Num bytes needed
	uint64 ProgressTotalBytes = 0;
	float ProgressPercent = 0;
};

struct FInstallBundleStatus
{
	FName BundleName;

	EInstallBundleStatus Status = EInstallBundleStatus::NotRequested;

	FText StatusText;

	// Progress is only present if Status is Downloading or Installing
	TOptional<FInstallBundleProgress> Progress;
};

struct FInstallBundleResultInfo
{
	FName BundleName;
	EInstallBundleResult Result = EInstallBundleResult::OK;

	// Currently, these just forward BPT Error info
	FText OptionalErrorText;
	FString OptionalErrorCode;
};

enum class EInstallBundleContentState : int
{
	InitializationError,
	NeedsUpdate,
	UpToDate,
};

struct FInstallBundleContentState
{
	EInstallBundleContentState State = EInstallBundleContentState::InitializationError;
	uint64 DownloadSize = 0;
};

enum class EInstallBundleRequestInfoFlags : int32
{
	None							= 0,
	EnqueuedBundlesForInstall		= (1 << 0),
	EnqueuedBundlesForRemoval		= (1 << 1),
	SkippedAlreadyMountedBundles	= (1 << 2),
	SkippedBundlesQueuedForRemoval	= (1 << 3),
	SkippedBundlesQueuedForInstall  = (1 << 4), // Only valid for removal requests
	SkippedUnknownBundles			= (1 << 5),
	InitializationError				= (1 << 6), // Can't enqueue because the bundle manager failed to initialize
};
ENUM_CLASS_FLAGS(EInstallBundleRequestInfoFlags);

struct FInstallBundleRequestInfo
{
	EInstallBundleRequestInfoFlags InfoFlags = EInstallBundleRequestInfoFlags::None;
	TArray<FName> BundlesQueuedForInstall;
	TArray<FName> BundlesQueuedForRemoval;
};

enum class EInstallBundleCancelFlags : int32
{
	None		= 0,
	Resumable	= (1 << 0),
};
ENUM_CLASS_FLAGS(EInstallBundleCancelFlags);

enum class EInstallBundleManagerInitErrorHandlerResult
{
	NotHandled, // Defer to the next handler
	Retry, // Try to initialize again
	StopInitialization, // Stop trying to initialize
};

DECLARE_DELEGATE_RetVal_OneParam(EInstallBundleManagerInitErrorHandlerResult, FInstallBundleManagerInitErrorHandler, EInstallBundleModuleInitResult);

DECLARE_MULTICAST_DELEGATE_OneParam(FInstallBundleCompleteMultiDelegate, FInstallBundleResultInfo);

DECLARE_DELEGATE_OneParam(FInstallBundleGetContentStateDelegate, FInstallBundleContentState);

class CORE_API IPlatformInstallBundleManager
{
public:
	static FInstallBundleCompleteMultiDelegate InstallBundleCompleteDelegate;
	static FInstallBundleCompleteMultiDelegate RemoveBundleCompleteDelegate;

	virtual ~IPlatformInstallBundleManager() {}

	virtual void PushInitErrorCallback(FInstallBundleManagerInitErrorHandler Callback) = 0;
	virtual void PopInitErrorCallback() = 0;

	virtual bool IsInitialized() const = 0;
	virtual bool IsInitializing() const = 0;

	virtual bool IsActive() const = 0;

	virtual FInstallBundleRequestInfo RequestUpdateContent(FName BundleName, EInstallBundleRequestFlags Flags) = 0;
	virtual FInstallBundleRequestInfo RequestUpdateContent(TArrayView<FName> BundleNames, EInstallBundleRequestFlags Flags) = 0;

	virtual void GetContentState(FName BundleName, bool bAddDependencies, FInstallBundleGetContentStateDelegate Callback) = 0;
	virtual void GetContentState(TArrayView<FName> BundleNames, bool bAddDependencies, FInstallBundleGetContentStateDelegate Callback) = 0;

	virtual FInstallBundleRequestInfo RequestRemoveBundle(FName BundleName) = 0;

	virtual void RequestRemoveBundleOnNextInit(FName BundleName) = 0;

	virtual void CancelBundle(FName BundleName, EInstallBundleCancelFlags Flags) = 0;

	virtual void CancelAllBundles(EInstallBundleCancelFlags Flags) = 0;

	virtual TOptional<FInstallBundleStatus> GetBundleProgress(FName BundleName) const = 0;

	virtual bool IsNullInterface() const = 0;

	virtual void SetErrorSimulationCommands(const FString& CommandLine) {}
};

class IPlatformInstallBundleManagerModule : public IModuleInterface
{
public:
	virtual void PreUnloadCallback() override
	{
		InstallBundleManager.Reset();
	}

	IPlatformInstallBundleManager* GetInstallBundleManager()
	{
		return InstallBundleManager.Get();
	}

protected:
	TUniquePtr<IPlatformInstallBundleManager> InstallBundleManager;
};

template<class PlatformInstallBundleManagerImpl>
class TPlatformInstallBundleManagerModule : public IPlatformInstallBundleManagerModule
{
public:
	virtual void StartupModule() override
	{
		// Only instantiate the bundle manager if this is the version the game has been configured to use
		FString ModuleName;
		GConfig->GetString(TEXT("InstallBundleManager"), TEXT("ModuleName"), ModuleName, GEngineIni);

		if (FModuleManager::Get().GetModule(*ModuleName) == this)
		{
			InstallBundleManager = MakeUnique<PlatformInstallBundleManagerImpl>();
		}
	}
};
