// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InstallBundleTypes.h"
#include "Misc/EnumClassFlags.h"
#include "Misc/ConfigCacheIni.h"
#include "Logging/LogMacros.h"
#include "Internationalization/Text.h"

class IAnalyticsProviderET;

struct FInstallBundleProgress
{
	FName BundleName;

	EInstallBundleStatus Status = EInstallBundleStatus::Requested;

	EInstallBundlePauseFlags PauseFlags = EInstallBundlePauseFlags::None;

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

	virtual bool HasBundleSource(EInstallBundleSourceType SourceType) const = 0;

	virtual FDelegateHandle PushInitErrorCallback(FInstallBundleManagerInitErrorHandler Callback) = 0;
	virtual void PopInitErrorCallback() = 0;
	virtual void PopInitErrorCallback(FDelegateHandle Handle) = 0;
	virtual void PopInitErrorCallback(const void* InUserObject) = 0;

	virtual EInstallBundleManagerInitState GetInitState() const = 0;

	FInstallBundleRequestInfo RequestUpdateContent(FName BundleName, EInstallBundleRequestFlags Flags);
	virtual FInstallBundleRequestInfo RequestUpdateContent(TArrayView<const FName> BundleNames, EInstallBundleRequestFlags Flags) = 0;

	void GetContentState(FName BundleName, EInstallBundleGetContentStateFlags Flags, bool bAddDependencies, FInstallBundleGetContentStateDelegate Callback, FName RequestTag = TEXT("None"));
	virtual void GetContentState(TArrayView<const FName> BundleNames, EInstallBundleGetContentStateFlags Flags, bool bAddDependencies, FInstallBundleGetContentStateDelegate Callback, FName RequestTag = TEXT("None")) = 0;

    virtual void CancelAllGetContentStateRequestsForTag(FName RequestTag) = 0;
    
	void RequestRemoveContentOnNextInit(FName RemoveName, TArrayView<const FName> KeepNames = TArrayView<const FName>());
	virtual void RequestRemoveContentOnNextInit(TArrayView<const FName> RemoveNames, TArrayView<const FName> KeepNames = TArrayView<const FName>()) = 0;

	void CancelRequestRemoveContentOnNextInit(FName BundleName);
	virtual void CancelRequestRemoveContentOnNextInit(TArrayView<const FName> BundleNames) = 0;

	void CancelUpdateContent(FName BundleName, EInstallBundleCancelFlags Flags);
	virtual void CancelUpdateContent(TArrayView<const FName> BundleNames, EInstallBundleCancelFlags Flags) = 0;

	void PauseUpdateContent(FName BundleName);
	virtual void PauseUpdateContent(TArrayView<const FName> BundleNames) = 0;

	void ResumeUpdateContent(FName BundleName);
	virtual void ResumeUpdateContent(TArrayView<const FName> BundleNames) = 0;

	virtual void RequestPausedBundleCallback() = 0;

	virtual TOptional<FInstallBundleProgress> GetBundleProgress(FName BundleName) const = 0;

	virtual EInstallBundleRequestFlags GetModifyableContentRequestFlags() const = 0;
	void UpdateContentRequestFlags(FName BundleName, EInstallBundleRequestFlags AddFlags, EInstallBundleRequestFlags RemoveFlags);
	virtual void UpdateContentRequestFlags(TArrayView<const FName> BundleNames, EInstallBundleRequestFlags AddFlags, EInstallBundleRequestFlags RemoveFlags) = 0;

	virtual bool IsNullInterface() const = 0;

	virtual void SetErrorSimulationCommands(const FString& CommandLine) {}

	virtual TSharedPtr<IAnalyticsProviderET> GetAnalyticsProvider() const { return TSharedPtr<IAnalyticsProviderET>(); }

	virtual void StartPersistentStatTrackingSession(const FString& SessionName, const TArray<FName>& RequiredBundles = TArray<FName>(), const FString& ExpectedAnalyticsID = FString(), bool bForceResetStatData = false) {}
	virtual void StopPersistentStatTrackingSession(const FString& SessionName) {}
};

