// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorDomain/EditorDomainUtils.h"

#include "Algo/IsSorted.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Containers/Array.h"
#include "DerivedDataCache.h"
#include "DerivedDataCacheInterface.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataCacheRecord.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackagePath.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinaryWriter.h"
#include "UObject/ObjectVersion.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace UE
{
namespace EditorDomain
{

// Change to a new guid when EditorDomain needs to be invalidated
const TCHAR* EditorDomainVersion = TEXT("C217EB656E9B4C04816D3DC0E21901F6");
// Identifier of the CacheBucket for EditorDomainPackages
const TCHAR* EditorDomainPackageBucketName = TEXT("EditorDomainPackage");

FPackageDigest GetPackageDigest(const FAssetPackageData& PackageData, FName PackageName)
{
	FCbWriter Writer;
	int32 CurrentFileVersionUE = GPackageFileUEVersion;
	int32 CurrentFileVersionLicenseeUE = GPackageFileLicenseeUEVersion;
	Writer << EditorDomainVersion;
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Writer << const_cast<FGuid&>(PackageData.PackageGuid);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	Writer << CurrentFileVersionUE;
	Writer << CurrentFileVersionLicenseeUE;
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
	AssetRegistry.WaitForPackage(PackageName.ToString());
	TOptional<FAssetPackageData> PackageData = AssetRegistry.GetAssetPackageDataCopy(PackageName);
	if (!PackageData)
	{
		return EPackageDigestResult::FileDoesNotExist;
	}
	OutPackageDigest = GetPackageDigest(*PackageData, PackageName);
	return EPackageDigestResult::Success;
}

UE::DerivedData::FCacheKey GetEditorDomainPackageKey(const FPackageDigest& PackageDigest)
{
	static UE::DerivedData::FCacheBucket EditorDomainPackageCacheBucket =
		GetDerivedDataCacheRef().CreateBucket(EditorDomainPackageBucketName);
	return UE::DerivedData::FCacheKey{EditorDomainPackageCacheBucket, PackageDigest};
}

UE::DerivedData::FRequest RequestEditorDomainPackage(const FPackagePath& PackagePath,
	const FPackageDigest& PackageDigest, UE::DerivedData::EPriority CachePriority,
	UE::DerivedData::FOnCacheGetComplete&& Callback)
{
	UE::DerivedData::ICache& Cache = GetDerivedDataCacheRef();
	return Cache.Get({ GetEditorDomainPackageKey(PackageDigest) },
		PackagePath.GetDebugName(),
		UE::DerivedData::ECachePolicy::QueryLocal, CachePriority, MoveTemp(Callback));
}

bool TrySavePackage(UPackage* Package)
{
	FPackageDigest PackageDigest;
	EPackageDigestResult FindHashResult = GetPackageDigest(*IAssetRegistry::Get(), Package->GetFName(), PackageDigest);
	switch (FindHashResult)
	{
	case EPackageDigestResult::Success:
		break;
	case EPackageDigestResult::FileDoesNotExist:
		return false;
	case EPackageDigestResult::WrongThread:
		return false;
	default:
		check(false);
		return false;
	}

	// EDITOR_DOMAIN_TODO: Need to extend SavePackage to allow saving to an archive
	FString TempFilename = FPaths::Combine(FPaths::ProjectIntermediateDir(), FGuid::NewGuid().ToString());
	ON_SCOPE_EXIT{ IFileManager::Get().Delete(*TempFilename); };

	uint32 SaveFlags = SAVE_NoError | // Do not crash the SaveServer on an error
		SAVE_BulkDataByReference; // EditorDomain saves reference bulkdata from the WorkspaceDomain rather than duplicating it

	bool bEditorDomainSaveUnversioned = false;
	GConfig->GetBool(TEXT("CookSettings"), TEXT("EditorDomainSaveUnversioned"), bEditorDomainSaveUnversioned, GEditorIni);
	if (bEditorDomainSaveUnversioned)
	{
		// With some exceptions, EditorDomain packages are saved unversioned; 
		// editors request the appropriate version of the EditorDomain package matching their serialization version
		bool bSaveUnversioned = true;
		TArray<UObject*> PackageObjects;
		GetObjectsWithPackage(Package, PackageObjects);
		for (UObject* Object : PackageObjects)
		{
			UClass* Class = Object ? Object->GetClass() : nullptr;
			if (Class && Class->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
			{
				// TODO: Revisit this once we track package schemas
				// Packages with Blueprint class instances can not be saved unversioned,
				// as the Blueprint class's layout can change during the editor's lifetime,
				// and we don't currently have a way to keep track of the changing package schema
				bSaveUnversioned = false;
			}
		}
		SaveFlags |= bSaveUnversioned ? SAVE_Unversioned : 0;
	}

	FSavePackageResultStruct Result = GEditor->Save(Package, nullptr, RF_Standalone, *TempFilename, GError,
		nullptr /* Conform */, false /* bForceByteSwapping */, true /* bWarnOfLongFilename */, SaveFlags);
	if (Result.Result != ESavePackageResult::Success)
	{
		return false;
	}

	FSharedBuffer PackageBuffer;
	{
		TArray64<uint8> TempBytes;
		bool bBytesRead = FFileHelper::LoadFileToArray(TempBytes, *TempFilename);
		IFileManager::Get().Delete(*TempFilename);
		if (!bBytesRead)
		{
			return false;
		}
		PackageBuffer = MakeSharedBufferFromArray(MoveTemp(TempBytes));
	}

	UE::DerivedData::ICache& Cache = GetDerivedDataCacheRef();
	UE::DerivedData::FCacheRecordBuilder RecordBuilder = Cache.CreateRecord(GetEditorDomainPackageKey(PackageDigest));
	TCbWriter<256> MetaData;
	MetaData.BeginObject();
	MetaData << "FileSize" << PackageBuffer.GetSize();
	MetaData.EndObject();
	RecordBuilder.SetMeta(MetaData.Save().AsObject());
	RecordBuilder.SetValue(PackageBuffer);
	UE::DerivedData::FCacheRecord Record = RecordBuilder.Build();
	Cache.Put(MakeArrayView(&Record, 1), Package->GetName());
	return true;
}

}
}
