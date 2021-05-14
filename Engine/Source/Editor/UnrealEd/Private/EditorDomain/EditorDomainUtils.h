// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ArrayView.h"
#include "Containers/UnrealString.h"
#include "DerivedDataCache.h"
#include "EditorDomain/EditorDomain.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/NameTypes.h"

DECLARE_LOG_CATEGORY_EXTERN(LogEditorDomainSave, Log, All);

class FAssetPackageData;
class FPackagePath;
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

/**
 * Calculate the PackageDigest for the given packagePath.
 * Reads information from the AssetRegistry to compute the digest.
 */
EPackageDigestResult GetPackageDigest(IAssetRegistry& AssetRegistry, FName PackageName,
	FPackageDigest& OutPackageDigest, FString& OutErrorMessage, FClassDigestMap& ClassDigests);
/** For any ClassNames not already in ClassDigests, look up their UStruct and add them. */
void PrecacheClassDigests(TConstArrayView<FName> ClassNames, FClassDigestMap& ClassDigests);

/** Convert the given PackageDigest into CacheKey format. */
UE::DerivedData::FCacheKey GetEditorDomainPackageKey(const FPackageDigest& PackageDigest);

/** Get the CacheRequest for the given package from the EditorDomain cache bucket. */
UE::DerivedData::FRequest RequestEditorDomainPackage(const FPackagePath& PackagePath,
	const FPackageDigest& PackageDigest, UE::DerivedData::EPriority CachePriority,
	UE::DerivedData::FOnCacheGetComplete&& Callback);

/** Save the given package into the EditorDomain. */
bool TrySavePackage(UPackage* Package, FClassDigestMap& ClassDigests);

}
