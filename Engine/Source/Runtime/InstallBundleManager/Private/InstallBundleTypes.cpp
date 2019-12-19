// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "InstallBundleTypes.h"
#include "InstallBundleManagerPrivatePCH.h"

#include "InstallBundleUtils.h"

const TCHAR* LexToString(EInstallBundleSourceType Type)
{
	static const TCHAR* Strings[] =
	{
		TEXT("Bulk"),
		TEXT("BuildPatchServices"),
	};

	static_assert(InstallBundleUtil::CastToUnderlying(EInstallBundleSourceType::Count) == UE_ARRAY_COUNT(Strings), "");
	return Strings[InstallBundleUtil::CastToUnderlying(Type)];
}

const TCHAR* LexToString(EInstallBundleManagerInitResult Result)
{
	static const TCHAR* Strings[] =
	{
		TEXT("OK"),
		TEXT("BuildMetaDataNotFound"),
		TEXT("BuildMetaDataDownloadError"),
		TEXT("BuildMetaDataParsingError"),
		TEXT("DistributionRootParseError"),
		TEXT("DistributionRootDownloadError"),
		TEXT("ManifestArchiveError"),
		TEXT("ManifestCreationError"),
		TEXT("ManifestDownloadError"),
		TEXT("BackgroundDownloadsIniDownloadError"),
		TEXT("NoInternetConnectionError"),
		TEXT("ConfigurationError"),
	};

	static_assert(InstallBundleUtil::CastToUnderlying(EInstallBundleManagerInitResult::Count) == UE_ARRAY_COUNT(Strings), "");
	return Strings[InstallBundleUtil::CastToUnderlying(Result)];
}

const TCHAR* LexToString(EInstallBundleContentState State)
{
	static const TCHAR* Strings[] =
	{
		TEXT("NotInstalled"),
		TEXT("NeedsUpdate"),
		TEXT("UpToDate"),
	};

	static_assert(InstallBundleUtil::CastToUnderlying(EInstallBundleContentState::Count) == UE_ARRAY_COUNT(Strings), "");
	return Strings[InstallBundleUtil::CastToUnderlying(State)];
}

const TCHAR* LexToString(EInstallBundleResult Result)
{
	static const TCHAR* Strings[] =
	{
		TEXT("OK"),
		TEXT("FailedPrereqRequiresLatestClient"),
		TEXT("InstallError"),
		TEXT("InstallerOutOfDiskSpaceError"),
		TEXT("ManifestArchiveError"),
		TEXT("UserCancelledError"),
		TEXT("InitializationError"),
	};

	static_assert(InstallBundleUtil::CastToUnderlying(EInstallBundleResult::Count) == UE_ARRAY_COUNT(Strings), "");
	return Strings[InstallBundleUtil::CastToUnderlying(Result)];
}

const TCHAR* LexToString(EInstallBundleStatus Status)
{
	static const TCHAR* Strings[] =
	{
		TEXT("Requested"),
		TEXT("Updating"),
		TEXT("Finishing"),
		TEXT("Ready"),
	};

	static_assert(InstallBundleUtil::CastToUnderlying(EInstallBundleStatus::Count) == UE_ARRAY_COUNT(Strings), "");
	return Strings[InstallBundleUtil::CastToUnderlying(Status)];
}

bool FInstallBundleCombinedContentState::GetAllBundlesHaveState(EInstallBundleContentState State, TArrayView<const FName> ExcludedBundles /*= TArrayView<const FName>()*/) const
{
	for (const TPair<FName, FInstallBundleContentState>& Pair : IndividualBundleStates)
	{
		if (ExcludedBundles.Contains(Pair.Key))
			continue;

		if (Pair.Value.State != State)
			return false;
	}

	return true;
}

bool FInstallBundleCombinedContentState::GetAnyBundleHasState(EInstallBundleContentState State, TArrayView<const FName> ExcludedBundles /*= TArrayView<const FName>()*/) const
{
	for (const TPair<FName, FInstallBundleContentState>& Pair : IndividualBundleStates)
	{
		if (ExcludedBundles.Contains(Pair.Key))
			continue;

		if (Pair.Value.State == State)
			return true;
	}

	return false;
}

