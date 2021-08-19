// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "UObject/NameTypes.h"

class IPackageStoreWriter;
class ITargetPlatform;
class FCbObject;
class FString;
class UPackage;
struct FIoHash;

namespace UE::TargetDomain
{

/** Create the TargetDomainKey based on the EditorDomainKeys of the Package and its dependencies. */
bool TryCreateKey(FName PackageName, TArrayView<FName> SortedBuildDependencies, FIoHash* OutHash, FString* OutErrorMessage);

/** Collect the Package's dependencies and the key based on them. */
bool TryCollectKeyAndDependencies(UPackage* Package, const ITargetPlatform* TargetPlatform,
	FIoHash* OutHash, TArray<FName>* OutBuildDependencies, TArray<FName>* OutRuntimeOnlyDependencies, FString* OutErrorMessage);
/** Collect the Package's dependencies, and create a FCbObject describing them for storage in the OpLog. */
FCbObject CollectDependenciesObject(UPackage* Package, const ITargetPlatform* TargetPlatform, FString* ErrorMessage);

/**
 * Read the oplog for the given packagename and fetch the dependencies and key out of it. Use the dependencies to calculate
 * the current key, and return the dependencies and key only if they key matches.
 */
bool TryFetchKeyAndDependencies(IPackageStoreWriter* PackageStore, FName PackageName, const ITargetPlatform* TargetPlatform,
	FIoHash* OutHash, TArray<FName>* OutBuildDependencies, TArray<FName>* OutRuntimeOnlyDependencies, FString* OutErrorMessage);

/** Return whether iterative cook is enabled for the given packagename, based on used-class allowlist/blocklist. */
bool IsIterativeEnabled(FName PackageName);

}
