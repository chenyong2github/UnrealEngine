// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EnumRange.h"

enum class EInstallBundleSourceType : int
{
	Bulk,
	BuildPatchServices,
	PlayGo,
	IntelligentDelivery,
	GameCustom,
	Count,
};
ENUM_RANGE_BY_COUNT(EInstallBundleSourceType, EInstallBundleSourceType::Count);
INSTALLBUNDLEMANAGER_API const TCHAR* LexToString(EInstallBundleSourceType Type);
INSTALLBUNDLEMANAGER_API void LexFromString(EInstallBundleSourceType& OutType, const TCHAR* String);

enum class EInstallBundleManagerInitState : int
{
	NotInitialized,
	Failed,
	Succeeded
};

enum class EInstallBundleManagerInitResult : int
{
	OK,
	BuildMetaDataNotFound,
	BuildMetaDataDownloadError,
	BuildMetaDataParsingError,
	DistributionRootParseError,
	DistributionRootDownloadError,
	ManifestArchiveError,
	ManifestCreationError,
	ManifestDownloadError,
	BackgroundDownloadsIniDownloadError,
	NoInternetConnectionError,
	ConfigurationError,
	Count
};
INSTALLBUNDLEMANAGER_API const TCHAR* LexToString(EInstallBundleManagerInitResult Result);

enum class EInstallBundleContentState : int
{
	NotInstalled,
	NeedsUpdate,
	UpToDate,
	Count,
};
INSTALLBUNDLEMANAGER_API const TCHAR* LexToString(EInstallBundleContentState State);

struct INSTALLBUNDLEMANAGER_API FInstallBundleContentState
{
	EInstallBundleContentState State = EInstallBundleContentState::NotInstalled;
	float Weight = 0.0f;
	TMap<EInstallBundleSourceType, FString> Version;
};

struct INSTALLBUNDLEMANAGER_API FInstallBundleCombinedContentState
{
	TMap<FName, FInstallBundleContentState> IndividualBundleStates;
	TMap<EInstallBundleSourceType, FString> CurrentVersion;
	uint64 DownloadSize = 0;
	uint64 InstallSize = 0;
	uint64 InstallOverheadSize = 0;
	uint64 FreeSpace = 0;

	bool GetAllBundlesHaveState(EInstallBundleContentState State, TArrayView<const FName> ExcludedBundles = TArrayView<const FName>()) const;	
	bool GetAnyBundleHasState(EInstallBundleContentState State, TArrayView<const FName> ExcludedBundles = TArrayView<const FName>()) const;
};

enum class EInstallBundleGetContentStateFlags : uint32
{
	None = 0,
	ForceNoPatching = (1 << 0),
};
ENUM_CLASS_FLAGS(EInstallBundleGetContentStateFlags);

DECLARE_DELEGATE_OneParam(FInstallBundleGetContentStateDelegate, FInstallBundleCombinedContentState);

enum class EInstallBundleRequestInfoFlags : int32
{
	None = 0,
	EnqueuedBundlesForInstall = (1 << 0),
	SkippedAlreadyMountedBundles = (1 << 1),
	SkippedAlreadyUpdatedBundles = (1 << 2), // Only possible with EInstallBundleRequestFlags::SkipMount
	SkippedUnknownBundles = (1 << 3),
	SkippedInvalidBundles = (1 << 4), // Bundle can't be used with this build
	SkippedUnusableLanguageBundles = (1 << 5), // Can't enqueue language bundles because of current system settings
	SkippedBundlesDueToBundleSource = (1 << 6), // A bundle source rejected a bundle for some reason
	InitializationError = (1 << 7), // Can't enqueue because the bundle manager failed to initialize
};
ENUM_CLASS_FLAGS(EInstallBundleRequestInfoFlags);

enum class EInstallBundleResult : int
{
	OK,
	FailedPrereqRequiresLatestClient,
	FailedPrereqRequiresLatestContent,
	InstallError,
	InstallerOutOfDiskSpaceError,
	ManifestArchiveError,
	UserCancelledError,
	InitializationError,
	Count,
};
INSTALLBUNDLEMANAGER_API const TCHAR* LexToString(EInstallBundleResult Result);

enum class EInstallBundleRequestFlags : uint32
{
	None = 0,
	CheckForCellularDataUsage = (1 << 0),
	UseBackgroundDownloads = (1 << 1),
	SendNotificationIfDownloadCompletesInBackground = (1 << 2),
	ForceNoPatching = (1 << 3),
	TrackPersistentBundleStats = (1 << 4),
	SkipMount = (1 << 5),
	Defaults = UseBackgroundDownloads,
};
ENUM_CLASS_FLAGS(EInstallBundleRequestFlags)

struct FInstallBundleRequestInfo
{
	EInstallBundleRequestInfoFlags InfoFlags = EInstallBundleRequestInfoFlags::None;
	TArray<FName> BundlesQueuedForInstall;
};

enum class EInstallBundleCancelFlags : uint32
{
	None = 0,
	Resumable = (1 << 0),
};
ENUM_CLASS_FLAGS(EInstallBundleCancelFlags);

enum class EInstallBundlePauseFlags : uint32
{
	None = 0,
	OnCellularNetwork = (1 << 0),
	NoInternetConnection = (1 << 1),
	UserPaused = (1 << 2)
};
ENUM_CLASS_FLAGS(EInstallBundlePauseFlags);

enum class EInstallBundleStatus : int
{
	Requested,
	Updating,
	Finishing,
	Ready,
	Count,
};
INSTALLBUNDLEMANAGER_API const TCHAR* LexToString(EInstallBundleStatus Status);

enum class EInstallBundleManagerPatchCheckResult : uint32
{
	/** No patch required */
	NoPatchRequired,
	/** Client Patch required to continue */
	ClientPatchRequired,
	/** Content Patch required to continue */
	ContentPatchRequired,
	/** Logged in user required for a patch check */
	NoLoggedInUser,
	/** Patch check failed */
	PatchCheckFailure,
	Count,
};
INSTALLBUNDLEMANAGER_API const TCHAR* LexToString(EInstallBundleManagerPatchCheckResult EnumVal);

/**
 * Enum used to describe download priority. Higher priorities will be downloaded first.
 * Note: Should always be kept in High -> Low priority order if adding more Priorities!
 */
enum class EInstallBundlePriority : uint8
{
	High,
	Normal,
	Low,
	Count
};
INSTALLBUNDLEMANAGER_API const TCHAR* LexToString(EInstallBundlePriority Priority);
INSTALLBUNDLEMANAGER_API bool LexTryParseString(EInstallBundlePriority& OutMode, const TCHAR* InBuffer);

struct FInstallBundleSourceInitInfo
{
	EInstallBundleManagerInitResult Result = EInstallBundleManagerInitResult::OK;
	bool bShouldUseFallbackSource = false;
};

struct FInstallBundleSourceAsyncInitInfo : public FInstallBundleSourceInitInfo
{
	// Reserved for future use
};

struct FInstallBundleSourceBundleInfo
{
	FName BundleName;
	FString BundleNameString;
	EInstallBundlePriority Priority = EInstallBundlePriority::Low;
	bool bIsStartup = false; // Only one startup bundle allowed.  All sources must agree on this.
	bool bDoPatchCheck = false; // This bundle should do a patch check and fail if it doesn't pass
	bool bBundleUpToDate = false; // Whether this bundle is up to date
};

struct FInstallBundleSourceBundleInfoQueryResultInfo
{
	TMap<FName, FInstallBundleSourceBundleInfo> SourceBundleInfoMap;
};

enum class EInstallBundleSourceUpdateBundleInfoResult : uint32
{
	OK,
	AlreadyMounted,
	AlreadyRequested,
	IllegalStartupBundle,
	Count,
};

struct FInstallBundleSourceRequestResultInfo
{
	FName BundleName;
	EInstallBundleResult Result = EInstallBundleResult::OK;

	// Forward any errors from the underlying implementation for a specific source
	// Currently, these just forward BPT Error info
	FText OptionalErrorText;
	FString OptionalErrorCode;

	TArray<FString> ContentPaths;
	TArray<FString> AdditionalRootDirs;
	// Support platforms that need shaderlibs in the physical FS
	TSet<FString> NonUFSShaderLibPaths;

	bool bContentWasInstalled = false;
	
	bool DidBundleSourceDoWork() const { return (ContentPaths.Num() != 0);} 
};

struct FInstallBundleSourceProgress
{
	FName BundleName;

	float Install_Percent = 0;
};

struct FInstallBundleSourcePauseInfo
{
	FName BundleName;
	EInstallBundlePauseFlags PauseFlags = EInstallBundlePauseFlags::None;
	// True if the bundle actually transitioned to/from paused,
	// which is different than the flags changing
	bool bDidPauseChange = false;
};

enum class EInstallBundleSourceBundleSkipReason : uint32
{
	None = 0,
	LanguageNotCurrent = (1 << 0), // The platform language must be changed to make it valid to request this bundle
	NotValid = (1 << 1), // Bundle can't be used with this build
};
ENUM_CLASS_FLAGS(EInstallBundleSourceBundleSkipReason);
