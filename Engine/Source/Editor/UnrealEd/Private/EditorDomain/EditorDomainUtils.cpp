// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorDomain/EditorDomainUtils.h"

#include "Algo/IsSorted.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Containers/Array.h"
#include "DerivedDataCacheInterface.h"
#include "DerivedDataCacheKey.h"
#include "Misc/PackagePath.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinaryWriter.h"
#include "UObject/ObjectVersion.h"

namespace UE
{
namespace EditorDomain
{

// Change to a new guid when EditorDomain needs to be invalidated
const TCHAR* EditorDomainVersion = TEXT("D3317196-098A-449B-B740-631D94CD7C93");
// Identifier of the CacheBucket for EditorDomainPackages
const TCHAR* EditorDomainPackageBucketName = TEXT("EditorDomainPackage");

FPackageDigest GetPackageDigest(const FAssetPackageData& PackageData, FName PackageName)
{
	FCbWriter Writer;
	int32 CurrentFileVersionUE4 = GPackageFileUE4Version;
	int32 CurrentFileVersionLicenseeUE4 = GPackageFileLicenseeUE4Version;
	Writer << EditorDomainVersion;
	Writer << const_cast<FGuid&>(PackageData.PackageGuid);
	Writer << CurrentFileVersionUE4;
	Writer << CurrentFileVersionLicenseeUE4;
	check(Algo::IsSorted(PackageData.GetCustomVersions()));
	for (const UE::AssetRegistry::FPackageCustomVersion& PackageVersion : PackageData.GetCustomVersions())
	{
		Writer << const_cast<FGuid&>(PackageVersion.Key);
		TOptional<FCustomVersion> CurrentVersion = FCurrentCustomVersions::Get(PackageVersion.Key);
		if (CurrentVersion.IsSet())
		{
			Writer << CurrentVersion->Version;
		}
		else
		{
			UE_LOG(LogEditorDomain, Error, TEXT("CustomVersion guid %s is used by package %s but is not ")
				TEXT("available in FCurrentCustomVersions. PackageDigest will set its version to 0."),
				*PackageVersion.Key.ToString(), *PackageName.ToString());
		}
	}
	return Writer.Save().GetRangeHash();
}

EPackageDigestResult GetPackageDigest(IAssetRegistry& AssetRegistry, FName PackageName,
	FPackageDigest& OutPackageDigest)
{
	if (!IsInGameThread())
	{
		return EPackageDigestResult::WrongThread;
	}
	AssetRegistry.WaitForPackage(PackageName.ToString());
	const FAssetPackageData* PackageData = AssetRegistry.GetAssetPackageData(PackageName);
	if (!PackageData)
	{
		return EPackageDigestResult::FileDoesNotExist;
	}
	OutPackageDigest = GetPackageDigest(*PackageData, PackageName);
	return EPackageDigestResult::Success;
}

UE::DerivedData::FCacheKey GetEditorDomainPackageKey(const FPackageDigest& PackageDigest)
{
	static UE::DerivedData::FCacheBucket EditorDomainPackageCacheBucket(EditorDomainPackageBucketName);
	return UE::DerivedData::FCacheKey(EditorDomainPackageCacheBucket, PackageDigest);
}

UE::DerivedData::FCacheRequest RequestEditorDomainPackage(const FPackagePath& PackagePath,
	const FPackageDigest& PackageDigest, UE::DerivedData::ECachePriority CachePriority,
	UE::DerivedData::FOnCacheGetComplete&& Callback)
{
	UE::DerivedData::ICache& Cache = GetDerivedDataCacheRef().GetCache();
	return Cache.Get({ GetEditorDomainPackageKey(PackageDigest) },
		PackagePath.GetDebugName(),
		UE::DerivedData::ECachePolicy::QueryLocal, CachePriority, MoveTemp(Callback));
}

}
}
