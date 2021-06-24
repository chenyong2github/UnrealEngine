// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ArrayView.h"
#include "Containers/UnrealString.h"
#include "DerivedDataCache.h"
#include "EditorDomain/EditorDomain.h"
#include "Misc/ConfigCacheIni.h"
#include "Templates/Function.h"
#include "UObject/NameTypes.h"

DECLARE_LOG_CATEGORY_EXTERN(LogEditorDomainSave, Log, All);

class FAssetPackageData;
class FPackagePath;
class FSharedBuffer;
class IAssetRegistry;
class UPackage;

namespace UE::EditorDomain
{

enum class EPackageDigestResult
{
	Success,
	FileDoesNotExist,
	MissingCustomVersion,
	MissingClass,
};

/** A UClass's data that is used in the EditorDomain Digest */
struct FClassDigestData
{
	FBlake3Hash SchemaHash;
	bool bNative;
};

/** Threadsafe cache of ClassName -> Digest data for calculating EditorDomain Digests */
struct FClassDigestMap
{
	TMap<FName, FClassDigestData> Map;
	FCriticalSection Lock;
};

/**
 * Calculate the PackageDigest for the given packagePath.
 * Reads information from the AssetRegistry to compute the digest.
 */
EPackageDigestResult GetPackageDigest(IAssetRegistry& AssetRegistry, FName PackageName,
	FPackageDigest& OutPackageDigest, FString& OutErrorMessage);
/** For any ClassNames not already in ClassDigests, look up their UStruct and add them. */
void PrecacheClassDigests(TConstArrayView<FName> ClassNames);

/** Get the cachekey for the EditorDomainPackage for the given PackageDigest. */
UE::DerivedData::FCacheKey GetEditorDomainPackageKey(const FPackageDigest& PackageDigest);

/** Get the cachekey for the EditorDomainBulkDataList for the given PackageDigest. */
UE::DerivedData::FCacheKey GetBulkDataListKey(const FPackageDigest& PackageDigest);

/** Get the CacheRequest for the given package from the EditorDomain cache bucket. */
UE::DerivedData::FRequest RequestEditorDomainPackage(const FPackagePath& PackagePath,
	const FPackageDigest& PackageDigest, UE::DerivedData::ECachePolicy SkipFlags,
	UE::DerivedData::EPriority CachePriority, UE::DerivedData::FOnCacheGetComplete&& Callback);

/** Save the given package into the EditorDomain. */
bool TrySavePackage(UPackage* Package);

/** Get the CacheRequest for the BulkDataList of the given package. */
UE::DerivedData::FRequest GetBulkDataList(FName PackageName, TUniqueFunction<void(FSharedBuffer Buffer)>&& Callback);

/** Write the data for the BulkDataList of the given package to the cache. */
void PutBulkDataList(FName PackageName, FSharedBuffer Buffer);

/** Accessor for the global ClassDigest map shared by systems needing to calculate PackageDigests. */
FClassDigestMap& GetClassDigests();

}
