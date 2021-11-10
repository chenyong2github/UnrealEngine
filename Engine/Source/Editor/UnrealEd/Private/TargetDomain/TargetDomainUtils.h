// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "DerivedDataBuildDefinition.h"
#include "Serialization/PackageWriter.h"
#include "Templates/Function.h"
#include "UObject/NameTypes.h"

class ICookedPackageWriter;
class ITargetPlatform;
class FCbObject;
class FString;
class UPackage;
struct FIoHash;
namespace UE::DerivedData { class FBuildDefinition; }

namespace UE::TargetDomain
{

void UtilsInitialize(bool bEditorDomainEnabled);

/** Create the TargetDomainKey based on the EditorDomainKeys of the Package and its dependencies. */
bool TryCreateKey(FName PackageName, TConstArrayView<FName> SortedBuildDependencies, FIoHash* OutHash, FString* OutErrorMessage);

/** Collect the Package's dependencies and the key based on them. */
bool TryCollectKeyAndDependencies(UPackage* Package, const ITargetPlatform* TargetPlatform,
	FIoHash* OutHash, TArray<FName>* OutBuildDependencies, TArray<FName>* OutRuntimeOnlyDependencies, FString* OutErrorMessage);
/** Collect the Package's dependencies, and create a FCbObject describing them for storage in the OpLog. */
FCbObject CollectDependenciesObject(UPackage* Package, const ITargetPlatform* TargetPlatform, FString* ErrorMessage);

/** Marshal the given BuildDefinitionList to FCbObject for storage in DDC. */
FCbObject BuildDefinitionListToObject(TConstArrayView<UE::DerivedData::FBuildDefinition> BuildDefinitionList);

struct FCookAttachments
{
	const ITargetPlatform* TargetPlatform;
	TArray<FName> BuildDependencies;
	TArray<FName> RuntimeOnlyDependencies;
	TArray<UE::DerivedData::FBuildDefinition> BuildDefinitionList;
	FIoHash StoredKey;
	bool bValid = false;

	void Reset()
	{
		BuildDependencies.Reset();
		RuntimeOnlyDependencies.Reset();
		BuildDefinitionList.Reset();
		bValid = false;
	}
	void Empty()
	{
		BuildDependencies.Empty();
		RuntimeOnlyDependencies.Empty();
		BuildDefinitionList.Empty();
		bValid = false;
	}
};
void FetchCookAttachments(TArrayView<FName> PackageNames, const ITargetPlatform* TargetPlatform, ICookedPackageWriter* PackageWriter,
	TUniqueFunction<void (FName PackageName, FCookAttachments&& Result)>&& Callback);

bool IsCookAttachmentsValid(FName PackageName, const FCookAttachments& CookAttachments);

/** Return whether iterative cook is enabled for the given packagename, based on used-class allowlist/blocklist. */
bool IsIterativeEnabled(FName PackageName);


/** Store extra information derived during save and used by the cooker for the given EditorDomain package. */
void CommitEditorDomainCookAttachments(FName PackageName, TArrayView<IPackageWriter::FCommitAttachmentInfo> Attachments);

}
