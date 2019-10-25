// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EnumRange.h"

enum class EInstallBundleSourceType : int
{
	Bulk,
	BuildPatchServices,
	Count
};
ENUM_RANGE_BY_COUNT(EInstallBundleSourceType, EInstallBundleSourceType::Count);
INSTALLBUNDLEMANAGER_API const TCHAR* LexToString(EInstallBundleSourceType Type);

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

// TODO: Needs to be renamed to EInstallBundleState
enum class EBundleState : int
{
	NotInstalled,
	NeedsUpdate,
	NeedsMount,
	Mounted,
	Count,
};
INSTALLBUNDLEMANAGER_API const TCHAR* LexToString(EBundleState Val);

enum class EInstallBundleContentState : int
{
	InitializationError,
	NotInstalled,
	NeedsUpdate,
	UpToDate,
	Count,
};
INSTALLBUNDLEMANAGER_API const TCHAR* LexToString(EInstallBundleContentState State);

struct INSTALLBUNDLEMANAGER_API FInstallBundleContentState
{
	EInstallBundleContentState State = EInstallBundleContentState::InitializationError;
	TMap<FName, EInstallBundleContentState> IndividualBundleStates;
	TMap<FName, float> IndividualBundleWeights;
	uint64 DownloadSize = 0;
	uint64 InstallSize = 0;
	uint64 InstallOverheadSize = 0;
	uint64 FreeSpace = 0;
};

enum class EInstallBundleGetContentStateFlags : uint32
{
	None = 0,
	ForceNoPatching = (1 << 0),
};
ENUM_CLASS_FLAGS(EInstallBundleGetContentStateFlags);

