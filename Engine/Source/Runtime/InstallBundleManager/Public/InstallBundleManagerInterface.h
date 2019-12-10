// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InstallBundleTypes.h"
#include "Misc/EnumClassFlags.h"
#include "Misc/ConfigCacheIni.h"
#include "Logging/LogMacros.h"
#include "Internationalization/Text.h"

class IAnalyticsProviderET;

enum class EInstallBundleStatus : int
{
	Requested,
	QueuedForDownload,
	Downloading,
	QueuedForInstall,
	Installing,
	QueuedForFinish,
	Finishing,
	Installed,
	Count,
};

inline const TCHAR* LexToString(EInstallBundleStatus Status)
{
	using UnderType = __underlying_type(EInstallBundleStatus);
	static const TCHAR* Strings[] =
	{
		TEXT("Requested"),
		TEXT("QueuedForDownload"),
		TEXT("Downloading"),
		TEXT("QueuedForInstall"),
		TEXT("Installing"),
		TEXT("QueuedForFinish"),
		TEXT("Finishing"),
		TEXT("Installed"),
	};

	static_assert(static_cast<UnderType>(EInstallBundleStatus::Count) == UE_ARRAY_COUNT(Strings), "");

	return Strings[static_cast<UnderType>(Status)];
}

struct FInstallBundleDownloadProgress
{
	// Num bytes received
	uint64 BytesDownloaded = 0;
	// Num bytes written to storage (<= BytesDownloaded)
	uint64 BytesDownloadedAndWritten = 0;
	// Num bytes needed
	uint64 TotalBytesToDownload = 0;
	// Num bytes that failed to download
	uint64 TotalBytesFailedToDownload = 0;
	float PercentComplete = 0;
};

struct FInstallBundleStatus
{
	FName BundleName;

	EInstallBundleStatus Status = EInstallBundleStatus::QueuedForDownload;

	EInstallBundlePauseFlags PauseFlags = EInstallBundlePauseFlags::None;

	FText StatusText;

	// Download progress of EInstallBundleStatus::Downloading
	// Will be set if Status >= EInstallBundleStatus::Downloading
	TOptional<FInstallBundleDownloadProgress> BackgroundDownloadProgress;

	// Download progress of EInstallBundleStatus::Install
	// Will be set if Status >= EInstallBundleStatus::Install
	// We may download during install if background downloads are turned off or fail
	// We may also do small downloads during install as a normal part of installation
	TOptional<FInstallBundleDownloadProgress> InstallDownloadProgress;

	float Install_Percent = 0;

	float Finishing_Percent = 0;
};

struct FInstallBundleRequestResultInfo
{
	FName BundleName;
	EInstallBundleResult Result = EInstallBundleResult::OK;
	bool bIsStartup = false;
	bool bContentWasInstalled = false;

	// Currently, these just forward BPT Error info
	FText OptionalErrorText;
	FString OptionalErrorCode;
};

struct FInstallBundlePauseInfo
{
	FName BundleName;
	EInstallBundlePauseFlags PauseFlags = EInstallBundlePauseFlags::None;
};

enum class EInstallBundleManagerInitErrorHandlerResult
{
	NotHandled, // Defer to the next handler
	Retry, // Try to initialize again
	StopInitialization, // Stop trying to initialize
};

DECLARE_DELEGATE_RetVal_OneParam(EInstallBundleManagerInitErrorHandlerResult, FInstallBundleManagerInitErrorHandler, EInstallBundleManagerInitResult);

DECLARE_MULTICAST_DELEGATE_OneParam(FInstallBundleCompleteMultiDelegate, FInstallBundleRequestResultInfo);
DECLARE_MULTICAST_DELEGATE_OneParam(FInstallBundlePausedMultiDelegate, FInstallBundlePauseInfo);

class INSTALLBUNDLEMANAGER_API IInstallBundleManager
{
public:
	static FInstallBundleCompleteMultiDelegate InstallBundleCompleteDelegate;
	static FInstallBundleCompleteMultiDelegate RemoveBundleCompleteDelegate;
	static FInstallBundlePausedMultiDelegate PausedBundleDelegate;

	static IInstallBundleManager* GetPlatformInstallBundleManager();

	virtual ~IInstallBundleManager() {}

	// TODO: should query if we are using the BPT source
	virtual bool HasBuildMetaData() const = 0;

	virtual FDelegateHandle PushInitErrorCallback(FInstallBundleManagerInitErrorHandler Callback) = 0;
	virtual void PopInitErrorCallback() = 0;
	virtual void PopInitErrorCallback(FDelegateHandle Handle) = 0;
	virtual void PopInitErrorCallback(const void* InUserObject) = 0;

	virtual EInstallBundleManagerInitState GetInitState() const = 0;

	virtual FInstallBundleRequestInfo RequestUpdateContent(FName BundleName, EInstallBundleRequestFlags Flags) = 0;
	virtual FInstallBundleRequestInfo RequestUpdateContent(TArrayView<const FName> BundleNames, EInstallBundleRequestFlags Flags) = 0;

	virtual void GetContentState(FName BundleName, EInstallBundleGetContentStateFlags Flags, bool bAddDependencies, FInstallBundleGetContentStateDelegate Callback, FName RequestTag = TEXT("None")) = 0;
	virtual void GetContentState(TArrayView<const FName> BundleNames, EInstallBundleGetContentStateFlags Flags, bool bAddDependencies, FInstallBundleGetContentStateDelegate Callback, FName RequestTag = TEXT("None")) = 0;

    virtual void CancelAllGetContentStateRequestsForTag(FName RequestTag) = 0;
    
	virtual void RequestRemoveContentOnNextInit(FName RemoveName, TArrayView<const FName> KeepNames = TArrayView<const FName>()) = 0;
	virtual void RequestRemoveContentOnNextInit(TArrayView<const FName> RemoveNames, TArrayView<const FName> KeepNames = TArrayView<const FName>()) = 0;

	virtual void CancelRequestRemoveContentOnNextInit(FName BundleName) = 0;
	virtual void CancelRequestRemoveContentOnNextInit(TArrayView<const FName> BundleNames) = 0;

	virtual void CancelUpdateContent(FName BundleName, EInstallBundleCancelFlags Flags) = 0;
	virtual void CancelUpdateContent(TArrayView<const FName> BundleNames, EInstallBundleCancelFlags Flags) = 0;

	virtual void PauseUpdateContent(FName BundleName) = 0;
	virtual void PauseUpdateContent(TArrayView<const FName> BundleNames) = 0;

	virtual void ResumeUpdateContent(FName BundleName) = 0;
	virtual void ResumeUpdateContent(TArrayView<const FName> BundleNames) = 0;

	virtual void RequestPausedBundleCallback() = 0;

	virtual TOptional<FInstallBundleStatus> GetBundleProgress(FName BundleName) const = 0;

	virtual EInstallBundleRequestFlags GetModifyableContentRequestFlags() const = 0;
	virtual void UpdateContentRequestFlags(FName BundleName, EInstallBundleRequestFlags AddFlags, EInstallBundleRequestFlags RemoveFlags) = 0;
	virtual void UpdateContentRequestFlags(TArrayView<const FName> BundleNames, EInstallBundleRequestFlags AddFlags, EInstallBundleRequestFlags RemoveFlags) = 0;

	virtual bool IsNullInterface() const = 0;

	virtual void SetErrorSimulationCommands(const FString& CommandLine) {}

	virtual TSharedPtr<IAnalyticsProviderET> GetAnalyticsProvider() const { return TSharedPtr<IAnalyticsProviderET>(); }
};

