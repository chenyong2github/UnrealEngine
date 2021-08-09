// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorDomain/EditorDomainUtils.h"

#include "Algo/IsSorted.h"
#include "Algo/Unique.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Containers/Array.h"
#include "DerivedDataCache.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataCacheRecord.h"
#include "DerivedDataRequestOwner.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "IO/PackageStoreWriter.h"
#include "Memory/SharedBuffer.h"
#include "Misc/FileHelper.h"
#include "Misc/PackagePath.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinaryWriter.h"
#include "UObject/CoreRedirects.h"
#include "UObject/ObjectVersion.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectHash.h"

namespace UE
{
namespace EditorDomain
{

FClassDigestMap GClassDigests;

FClassDigestMap& GetClassDigests()
{
	return GClassDigests;
}

// Change to a new guid when EditorDomain needs to be invalidated
const TCHAR* EditorDomainVersion = TEXT("D1718C34CA7C47AEB87A1607568E25B0");
// Identifier of the CacheBuckets for EditorDomain tables
const TCHAR* EditorDomainPackageBucketName = TEXT("EditorDomainPackage");
const TCHAR* EditorDomainBulkDataListBucketName = TEXT("EditorDomainBulkDataList");
const TCHAR* EditorDomainBulkDataPayloadIdBucketName = TEXT("EditorDomainBulkDataPayloadId");

static bool GetEditorDomainSaveUnversioned()
{
	auto Initialize = []()
	{
		bool bParsedValue;
		bool bResult = GConfig->GetBool(TEXT("EditorDomain"), TEXT("SaveUnversioned"), bParsedValue, GEditorIni) ? bParsedValue : true;
		if (GConfig->GetBool(TEXT("CookSettings"), TEXT("EditorDomainSaveUnversioned"), bResult, GEditorIni))
		{
			UE_LOG(LogEditorDomain, Error, TEXT("Editor.ini:[CookSettings]:EditorDomainSaveUnversioned is deprecated, use Editor.ini:[EditorDomain]:SaveUnversioned instead."));
		}
		return bResult;
	};
	static bool bEditorDomainSaveUnversioned = Initialize();
	return bEditorDomainSaveUnversioned;
}

EPackageDigestResult AppendPackageDigest(FCbWriter& Writer, bool& bOutIsBlacklisted, FString& OutErrorMessage,
	const FAssetPackageData& PackageData, FName PackageName)
{
	bOutIsBlacklisted = false;

	int32 CurrentFileVersionUE = GPackageFileUEVersion;
	int32 CurrentFileVersionLicenseeUE = GPackageFileLicenseeUEVersion;
	Writer << EditorDomainVersion;
	Writer << GetEditorDomainSaveUnversioned();
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
			OutErrorMessage = FString::Printf(TEXT("Package %s uses CustomVersion guid %s but that guid is not available in FCurrentCustomVersions"),
				*PackageName.ToString(), *PackageVersion.Key.ToString());
			return EPackageDigestResult::MissingCustomVersion;
		}
	}
	FNameBuilder NameBuilder;
	int32 NextClass = 0;
	FClassDigestMap& ClassDigests = GetClassDigests();
	for (int32 Attempt = 0; NextClass < PackageData.ImportedClasses.Num(); ++Attempt)
	{
		if (Attempt > 0)
		{
			// EDITORDOMAIN_TODO: Remove this !IsInGameThread check once FindObject no longer asserts if GIsSavingPackage
			if (Attempt > 1 || !IsInGameThread())
			{
				OutErrorMessage = FString::Printf(TEXT("Package %s uses Class %s but that class is not loaded"),
					*PackageName.ToString(), *PackageData.ImportedClasses[NextClass].ToString());
				return EPackageDigestResult::MissingClass;
			}
			TConstArrayView<FName> RemainingClasses(PackageData.ImportedClasses);
			RemainingClasses = RemainingClasses.Slice(NextClass, PackageData.ImportedClasses.Num() - NextClass);
			PrecacheClassDigests(RemainingClasses);
		}
		FScopeLock ClassDigestsScopeLock(&ClassDigests.Lock);
		for (; NextClass < PackageData.ImportedClasses.Num(); ++NextClass)
		{
			FName ClassName = PackageData.ImportedClasses[NextClass];
			FClassDigestData* ExistingData = ClassDigests.Map.Find(ClassName);
			if (!ExistingData)
			{
				break;
			}
			if (ExistingData->bNative)
			{
				Writer << ExistingData->SchemaHash;
			}
			bOutIsBlacklisted |= ExistingData->bBlacklisted;
		}
	}
	return EPackageDigestResult::Success;
}

void PrecacheClassDigests(TConstArrayView<FName> ClassNames, TMap<FName, FClassDigestData>* OutDatas)
{
	FClassDigestMap& ClassDigests = GetClassDigests();
	FString ClassNameStr;
	TArray<FName, TInlineAllocator<10>> ClassesToAdd;
	ClassesToAdd.Reserve(ClassNames.Num());
	{
		FScopeLock ClassDigestsScopeLock(&ClassDigests.Lock);
		for (FName ClassName : ClassNames)
		{
			FClassDigestData* DigestData = ClassDigests.Map.Find(ClassName);
			if (DigestData)
			{
				if (OutDatas)
				{
					OutDatas->Add(ClassName, *DigestData);
				}
			}
			else
			{
				ClassesToAdd.Add(ClassName);
			}
		}
	}
	if (ClassesToAdd.Num() == 0)
	{
		return;
	}

	struct FClassData
	{
		FName Name;
		FName ParentName;
		UStruct* ParentStruct = nullptr;
		FClassDigestData DigestData;
	};
	TArray<FClassData, TInlineAllocator<10>> ClassDatas;
	FString NameStringBuffer;
	IAssetRegistry& AssetRegistry = *IAssetRegistry::Get();
	TArray<FName> AncestorShortNames;

	ClassDatas.Reserve(ClassesToAdd.Num());
	for (FName ClassName : ClassesToAdd)
	{
		FName LookupName = ClassName;
		ClassName.ToString(NameStringBuffer);
		FCoreRedirectObjectName ClassNameRedirect(NameStringBuffer);
		FCoreRedirectObjectName RedirectedClassNameRedirect = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class, ClassNameRedirect);
		if (ClassNameRedirect != RedirectedClassNameRedirect)
		{
			NameStringBuffer = RedirectedClassNameRedirect.ToString();
			LookupName = FName(NameStringBuffer);
		}
		UStruct* Struct = nullptr;
		if (FPackageName::IsScriptPackage(NameStringBuffer))
		{
			Struct = FindObject<UStruct>(nullptr, *NameStringBuffer);
			if (!Struct)
			{
				// If a native class is not found we do not put it in our results
				continue;
			}
		}
		
		FClassData& ClassData = ClassDatas.Emplace_GetRef();
		ClassData.Name = ClassName;
		ClassData.DigestData.bBlacklisted = GetClassBlacklist().Contains(ClassName);
		if (LookupName != ClassName)
		{
			ClassData.DigestData.bBlacklisted |= GetClassBlacklist().Contains(LookupName);
		}

		if (Struct)
		{
			ClassData.DigestData.bNative = true;
			ClassData.DigestData.SchemaHash = Struct->GetSchemaHash(false /* bSkipEditorOnly */);
			ClassData.ParentStruct = Struct->GetSuperStruct();
			if (ClassData.ParentStruct)
			{
				NameStringBuffer.Reset();
				ClassData.ParentStruct->GetPathName(nullptr, NameStringBuffer);
				ClassData.ParentName = FName(*NameStringBuffer);
			}
		}
		else
		{
			ClassData.DigestData.bNative = false;
			ClassData.DigestData.SchemaHash.Reset();
			FStringView UnusedClassOfClassName;
			FStringView ClassPackageName;
			FStringView ClassObjectName;
			FStringView ClassSubObjectName;
			FPackageName::SplitFullObjectPath(NameStringBuffer, UnusedClassOfClassName, ClassPackageName, ClassObjectName, ClassSubObjectName);
			FName ClassObjectFName(ClassObjectName);
			// TODO_EDITORDOMAIN: If the class is not yet present in the assetregistry, or
			// if its parent classes are not, then we will not be able to propagate information from the parent classes; wait on the class to be parsed
			AncestorShortNames.Reset();
			IAssetRegistry::Get()->GetAncestorClassNames(ClassObjectFName, AncestorShortNames);
			for (FName ShortName : AncestorShortNames)
			{
				// TODO_EDITORDOMAIN: For robustness and performance, we need the AssetRegistry to return FullPathNames rather than ShortNames
				// For now, we lookup each shortname using FindObject, and do not handle propagating data from blueprint classes to child classes
				if (UStruct* ParentStruct = FindObjectFast<UStruct>(nullptr, ShortName, false /* ExactClass */, true /* AnyPackage */))
				{
					NameStringBuffer.Reset();
					ParentStruct->GetPathName(nullptr, NameStringBuffer);
					if (FPackageName::IsScriptPackage(NameStringBuffer))
					{
						ClassData.ParentStruct = ParentStruct;
						ClassData.ParentName = FName(*NameStringBuffer);
						break;
					}
				}
			}
		}
	}

	TMap<FName, FClassData> RemainingBatch;
	{
		FScopeLock ClassDigestsScopeLock(&ClassDigests.Lock);

		// Look up the data for the parent of each class, so we can propagate bBlacklisted from the parent,
		// once parent data is propagated, add it to the ClassDigests map.
		// For any parents missing data, keep the class for a second pass that adds the parent
		for (FClassData& ClassData : ClassDatas)
		{
			bool bNeedsParent = false;
			if (!ClassData.ParentName.IsNone())
			{
				FClassDigestData* ParentDigest = ClassDigests.Map.Find(ClassData.ParentName);
				if (ParentDigest)
				{
					ClassData.DigestData.bBlacklisted |= ParentDigest->bBlacklisted;
				}
				else
				{
					bNeedsParent = true;
				}
			}
			if (!bNeedsParent)
			{
				if (OutDatas)
				{
					OutDatas->Add(ClassData.Name, ClassData.DigestData);
				}
				ClassDigests.Map.Add(ClassData.Name, MoveTemp(ClassData.DigestData));
			}
			else
			{
				RemainingBatch.Add(ClassData.Name, MoveTemp(ClassData));
			}
		}
	}

	if (RemainingBatch.Num() == 0)
	{
		return;
	}

	// Get all unique ancestors (skipping those that are already in the batch) and recursively cache them
	TSet<FName> Parents;
	for (const TPair<FName, FClassData>& RemainingPair : RemainingBatch)
	{
		const FClassData& ClassData = RemainingPair.Value;
		if (ClassData.ParentName.IsNone() || RemainingBatch.Contains(ClassData.ParentName))
		{
			continue;
		}
		checkf(ClassData.ParentStruct, TEXT("If the ClassData has a parent, it should have come either from the ParentStruct."));
		FName ParentName = ClassData.ParentName;
		UStruct* ParentStruct = ClassData.ParentStruct;
		do
		{
			bool bAlreadyExists;
			Parents.Add(ParentName, &bAlreadyExists);
			if (bAlreadyExists)
			{
				break;
			}
			ParentStruct = ParentStruct->GetSuperStruct();
			if (ParentStruct)
			{
				NameStringBuffer.Reset();
				ParentStruct->GetPathName(nullptr, NameStringBuffer);
				ParentName = FName(*NameStringBuffer);
			}
		}
		while (ParentStruct);
	}
	TMap<FName, FClassDigestData> ParentDigests;
	PrecacheClassDigests(Parents.Array(), &ParentDigests);

	// Propagate parent values to children, pulling parentdata from ParentDigests or RemainingBatch
	TSet<FName> Visited;
	auto RecursivePropagate = [&RemainingBatch, &ParentDigests, &Visited](FClassData& ClassData, auto& RecursivePropagateReference)
	{
		bool bAlreadyInSet;
		Visited.Add(ClassData.Name, &bAlreadyInSet);
		if (bAlreadyInSet)
		{
			return;
		}
		FClassDigestData* ParentDigest = ParentDigests.Find(ClassData.ParentName);
		if (!ParentDigest)
		{
			FClassData* ParentData = RemainingBatch.Find(ClassData.ParentName);
			if (ParentData)
			{
				RecursivePropagateReference(*ParentData, RecursivePropagateReference);
				ParentDigest = &ParentData->DigestData;
			}
		}
		// If the superclass was not found, due to a bad redirect or a missing blueprint assetregistry entry, then give up and treat the class as having no parent
		if (ParentDigest)
		{
			ClassData.DigestData.bBlacklisted |= ParentDigest->bBlacklisted;
		}
	};
	for (TPair<FName, FClassData>& RemainingPair : RemainingBatch)
	{
		RecursivePropagate(RemainingPair.Value, RecursivePropagate);
	}

	// Add the now-complete RemainingBatch digests to ClassDigests
	{
		FScopeLock ClassDigestsScopeLock(&ClassDigests.Lock);
		for (TPair<FName, FClassData>& RemainingPair : RemainingBatch)
		{
			if (OutDatas)
			{
				OutDatas->Add(RemainingPair.Key, RemainingPair.Value.DigestData);
			}
			ClassDigests.Map.Add(RemainingPair.Key, MoveTemp(RemainingPair.Value.DigestData));
		}
	}
}

TSet<FName> ConstructClassBlacklist()
{
	TSet<FName> Result;
	TArray<FString> BlacklistArray;
	GConfig->GetArray(TEXT("EditorDomain"), TEXT("ClassBlacklist"), BlacklistArray, GEditorIni);
	for (const FString& ClassPathName : BlacklistArray)
	{
		Result.Add(FName(*ClassPathName));
	}
	return Result;
}

const TSet<FName>& GetClassBlacklist()
{
	static TSet<FName> ClassBlacklist = ConstructClassBlacklist();
	return ClassBlacklist;
}

TSet<FName> ConstructPackageNameBlacklist()
{
	TSet<FName> Result;
	TArray<FString> BlacklistArray;
	GConfig->GetArray(TEXT("EditorDomain"), TEXT("PackageBlacklist"), BlacklistArray, GEditorIni);
	FString PackageName;
	FString ErrorReason;
	for (const FString& PackageNameOrFilename : BlacklistArray)
	{
		if (!FPackageName::TryConvertFilenameToLongPackageName(PackageNameOrFilename, PackageName, &ErrorReason))
		{
			UE_LOG(LogEditorDomain, Warning, TEXT("Editor.ini:[EditorDomain]:PackageBlacklist: Could not convert %s to a LongPackageName: %s"),
				*PackageNameOrFilename, *ErrorReason);
			continue;
		}
		Result.Add(FName(*PackageName));
	}
	return Result;
}

const TSet<FName>& GetPackageNameBlacklist()
{
	static TSet<FName> PackageNameBlacklist = ConstructPackageNameBlacklist();
	return PackageNameBlacklist;
}

EPackageDigestResult GetPackageDigest(IAssetRegistry& AssetRegistry, FName PackageName,
	FPackageDigest& OutPackageDigest, bool& bOutIsBlacklisted, FString& OutErrorMessage)
{
	FCbWriter Builder;
	EPackageDigestResult Result = AppendPackageDigest(AssetRegistry, PackageName, Builder, bOutIsBlacklisted, OutErrorMessage);
	if (Result == EPackageDigestResult::Success)
	{
		OutPackageDigest = Builder.Save().GetRangeHash();
	}
	return Result;
}

EPackageDigestResult AppendPackageDigest(IAssetRegistry& AssetRegistry, FName PackageName,
	FCbWriter& Builder, bool& bOutIsBlacklisted, FString& OutErrorMessage)
{
	AssetRegistry.WaitForPackage(PackageName.ToString());
	TOptional<FAssetPackageData> PackageData = AssetRegistry.GetAssetPackageDataCopy(PackageName);
	if (!PackageData)
	{
		OutErrorMessage = FString::Printf(TEXT("Package %s does not exist in the AssetRegistry"),
			*PackageName.ToString());
		bOutIsBlacklisted = false;
		return EPackageDigestResult::FileDoesNotExist;
	}
	EPackageDigestResult Result = AppendPackageDigest(Builder, bOutIsBlacklisted, OutErrorMessage, *PackageData, PackageName);
	bOutIsBlacklisted |= GetPackageNameBlacklist().Contains(PackageName);
	return Result;
}

UE::DerivedData::FCacheKey GetEditorDomainPackageKey(const FPackageDigest& PackageDigest)
{
	static UE::DerivedData::FCacheBucket EditorDomainPackageCacheBucket(EditorDomainPackageBucketName);
	return UE::DerivedData::FCacheKey{EditorDomainPackageCacheBucket, PackageDigest};
}

UE::DerivedData::FCacheKey GetBulkDataListKey(const FPackageDigest& PackageDigest)
{
	static UE::DerivedData::FCacheBucket EditorDomainBulkDataListBucket(EditorDomainBulkDataListBucketName);
	return UE::DerivedData::FCacheKey{ EditorDomainBulkDataListBucket, PackageDigest };
}

UE::DerivedData::FCacheKey GetBulkDataPayloadIdKey(const FIoHash& PackageAndGuidDigest)
{
	static UE::DerivedData::FCacheBucket EditorDomainBulkDataPayloadIdBucket(EditorDomainBulkDataPayloadIdBucketName);
	return UE::DerivedData::FCacheKey{ EditorDomainBulkDataPayloadIdBucket, PackageAndGuidDigest };
}

void RequestEditorDomainPackage(const FPackagePath& PackagePath,
	const FPackageDigest& PackageDigest, UE::DerivedData::ECachePolicy SkipFlags, UE::DerivedData::IRequestOwner& Owner,
	UE::DerivedData::FOnCacheGetComplete&& Callback)
{
	UE::DerivedData::ICache& Cache = UE::DerivedData::GetCache();
	checkf((SkipFlags & (~UE::DerivedData::ECachePolicy::SkipData)) == UE::DerivedData::ECachePolicy::None,
		TEXT("SkipFlags should only contain ECachePolicy::Skip* flags"));
	Cache.Get({ GetEditorDomainPackageKey(PackageDigest) },
		PackagePath.GetDebugName(),
		SkipFlags | UE::DerivedData::ECachePolicy::QueryLocal, Owner, MoveTemp(Callback));
}

/** Stores data from SavePackage in accessible fields */
class FMemoryPackageStoreWriter : public IPackageStoreWriter
{
public:
	// IPackageStoreWriter
	virtual bool IsAdditionalFilesNeedLinkerSize() const override { return true; }
	virtual bool IsLinkerAdditionalDataInSeparateArchive() const override { return true; }

	virtual void WritePackageData(const FPackageInfo& Info, const FIoBuffer& PackageData, const TArray<FFileRegion>& FileRegions) override;
	virtual void WriteBulkdata(const FBulkDataInfo& Info, const FIoBuffer& BulkData, const TArray<FFileRegion>& FileRegions) override;
	virtual void WriteLinkerAdditionalData(const FLinkerAdditionalDataInfo& Info, const FIoBuffer& Data, const TArray<FFileRegion>& FileRegions) override;
	virtual void Flush() override
	{
	}
	virtual void BeginPackage(const FBeginPackageInfo& Info) override;
	virtual void CommitPackage(const FCommitPackageInfo& Info) override
	{
	}

	// IPackageStoreWriter cooking and accessor interface: not implemented in this writer
	virtual bool WriteAdditionalFile(const FAdditionalFileInfo& Info, const FIoBuffer& FileData) override
	{
		checkNoEntry();
		return false;
	}
	virtual void BeginCook(const FCookInfo& Info) override
	{
		checkNoEntry();
	}
	virtual void EndCook() override
	{
		checkNoEntry();
	}
	virtual void GetEntries(TFunction<void(TArrayView<const FPackageStoreEntryResource>)>&&) override
	{
		checkNoEntry();
	}
	DECLARE_DERIVED_EVENT(FMemoryPackageStoreWriter, IPackageStoreWriter::FCommitEvent, FCommitEvent);
	virtual FCommitEvent& OnCommit() override
	{
		checkNoEntry();
		return CommitEvent;
	}
	virtual void GetCookedPackages(TArray<FCookedPackageInfo>& OutCookedPackages) override
	{
		checkNoEntry();
	}
	virtual void RemoveCookedPackages(TArrayView<const FName> PackageNamesToRemove) override
	{
		checkNoEntry();
	}

	// FMemoryPackageStoreWriter interface
	uint64 GetHeaderSize() const { return HeaderSize; }
	FSharedBuffer& GetHeaderAndExports() { return HeaderAndExports; }
	/**
	 * BulkDatas in this array are views into the FMemoryPackageStoreWriter.
	 * Callers should call MakeOwned if they make copies that need to outlive the FMemoryPackageStoreWriter.
	 */
	TArray<FSharedBuffer>& GetBulkDatas() { return BulkDataRegions; }

private:
	void SetPackageName(FName InPackageName);

	FSharedBuffer HeaderAndExports;
	TArray<FSharedBuffer> BulkDataRegions;
	FCommitEvent CommitEvent;
	FName PackageName;
	uint64 HeaderSize = 0;
};

void FMemoryPackageStoreWriter::SetPackageName(FName InPackageName)
{
	if (PackageName.IsNone())
	{
		PackageName = InPackageName;
	}
	else
	{
		checkf(PackageName == InPackageName, TEXT("FMemoryPackageStoreWriter received different PackageNames in WritePackageData and WriteBulkdata."));
	}
}

void FMemoryPackageStoreWriter::BeginPackage(const FBeginPackageInfo& Info)
{
	SetPackageName(Info.PackageName);
}

auto IoBufferToSharedBuffer = [](const FIoBuffer& InBuffer) -> FSharedBuffer {
	InBuffer.EnsureOwned();
	const uint64 DataSize = InBuffer.DataSize();
	FIoBuffer MutableBuffer(InBuffer);
	uint8* DataPtr = MutableBuffer.Release().ValueOrDie();
	return FSharedBuffer{ FSharedBuffer::TakeOwnership(DataPtr, DataSize, FMemory::Free) };
};

void FMemoryPackageStoreWriter::WritePackageData(const FPackageInfo& Info, const FIoBuffer& PackageData, const TArray<FFileRegion>& FileRegions)
{
	for (const FFileRegion& FileRegion : FileRegions)
	{
		checkf(FileRegion.Type == EFileRegionType::None, TEXT("FMemoryPackageStoreWriter does not currently support FileRegion types other than None."));
	}
	SetPackageName(Info.PackageName);
	// Info.LooseFilePath is ignored
	HeaderSize = Info.HeaderSize;
	// Info.ChunkId is ignored
	HeaderAndExports = IoBufferToSharedBuffer(PackageData);
}

void FMemoryPackageStoreWriter::WriteBulkdata(const FBulkDataInfo& Info, const FIoBuffer& BulkData, const TArray<FFileRegion>& FileRegions)
{
	SetPackageName(Info.PackageName);
	// Info.LooseFilePath is ignored
	// Info.ChunkId is ignored
	checkf(Info.BulkdataType == IPackageStoreWriter::FBulkDataInfo::Standard, TEXT("MemoryPackageStoreWriter does not currently support BulkData types other than Standard."));

	FSharedBuffer BulkDataOwner = IoBufferToSharedBuffer(BulkData);
	const uint8* BulkDataStart = reinterpret_cast<const uint8*>(BulkDataOwner.GetData());
	uint64 BulkDataLen = BulkDataOwner.GetSize();
	for (const FFileRegion& FileRegion : FileRegions)
	{
		checkf(FileRegion.Type == EFileRegionType::None, TEXT("FMemoryPackageStoreWriter does not currently support FileRegion types other than None."));
		checkf(FileRegion.Offset + FileRegion.Length <= BulkDataLen, TEXT("FileRegions in WriteBulkdata were outside of the range of the BulkData's size."));
		check(FileRegion.Length > 0); // SavePackage is not allowed to call WriteBulkData with empty bulkdatas
		BulkDataRegions.Add(FSharedBuffer::MakeView(BulkDataStart + FileRegion.Offset, FileRegion.Length, BulkDataOwner));
	}
}

void FMemoryPackageStoreWriter::WriteLinkerAdditionalData(const FLinkerAdditionalDataInfo& Info, const FIoBuffer& Data, const TArray<FFileRegion>& FileRegions)
{
	SetPackageName(Info.PackageName);

	FSharedBuffer DataOwner = IoBufferToSharedBuffer(Data);
	const uint8* DataStart = reinterpret_cast<const uint8*>(DataOwner.GetData());
	uint64 DataLen = DataOwner.GetSize();
	for (const FFileRegion& FileRegion : FileRegions)
	{
		checkf(FileRegion.Type == EFileRegionType::None, TEXT("FMemoryPackageStoreWriter does not currently support FileRegion types other than None."));
		checkf(FileRegion.Offset + FileRegion.Length <= DataLen, TEXT("FileRegions in WriteLinkerAdditionalData were outside of the range of the Data's size."));
		check(FileRegion.Length > 0); // SavePackage is not allowed to call WriteLinkerAdditionalData with empty regions
		BulkDataRegions.Add(FSharedBuffer::MakeView(DataStart + FileRegion.Offset, FileRegion.Length, DataOwner));
	}

}

bool TrySavePackage(UPackage* Package)
{
	using namespace UE::DerivedData;

	FString ErrorMessage;
	FPackageDigest PackageDigest;
	bool bIsBlacklisted;
	EPackageDigestResult FindHashResult = GetPackageDigest(*IAssetRegistry::Get(), Package->GetFName(), PackageDigest,
		bIsBlacklisted, ErrorMessage);
	switch (FindHashResult)
	{
	case EPackageDigestResult::Success:
		break;
	default:
		UE_LOG(LogEditorDomain, Warning, TEXT("Could not save package to EditorDomain: %s."), *ErrorMessage)
		return false;
	}
	if (bIsBlacklisted)
	{
		UE_LOG(LogEditorDomain, Verbose, TEXT("Skipping save of blacklisted package to EditorDomain: %s."), *Package->GetName())
		return false;
	}

	uint32 SaveFlags = SAVE_NoError // Do not crash the SaveServer on an error
		| SAVE_BulkDataByReference	// EditorDomain saves reference bulkdata from the WorkspaceDomain rather than duplicating it
		| SAVE_Async				// SavePackage support for PackageStoreWriter is only implemented with SAVE_Async
		// EDITOR_DOMAIN_TODO: Add a a save flag that specifies the creation of a deterministic guid
		// | SAVE_KeepGUID;			// Prevent indeterminism by keeping the Guid
		;

	if (GetEditorDomainSaveUnversioned())
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
				// EDITOR_DOMAIN_TODO: Revisit this once we track package schemas
				// Packages with Blueprint class instances can not be saved unversioned,
				// as the Blueprint class's layout can change during the editor's lifetime,
				// and we don't currently have a way to keep track of the changing package schema
				bSaveUnversioned = false;
			}
		}
		SaveFlags |= bSaveUnversioned ? SAVE_Unversioned : 0;
	}

	FMemoryPackageStoreWriter* PackageStoreWriter = new FMemoryPackageStoreWriter();
	FSavePackageContext SavePackageContext(nullptr /* TargetPlatform */, PackageStoreWriter, false /* bInForceLegacyOffsets */);
	FSavePackageResultStruct Result = GEditor->Save(Package, nullptr, RF_Standalone, TEXT("EditorDomainPackageStoreWriter"),
		GError, nullptr /* Conform */, false /* bForceByteSwapping */, true /* bWarnOfLongFilename */,
		SaveFlags, nullptr /* TargetPlatform */, FDateTime::MinValue(), false /* bSlowTask */,
		nullptr /* DiffMap */, &SavePackageContext);
	if (Result.Result != ESavePackageResult::Success)
	{
		return false;
	}

	ICache& Cache = GetCache();
	FCacheRecordBuilder RecordBuilder(GetEditorDomainPackageKey(PackageDigest));

	// We use a counter for PayloadIds rather than hashes of the Attachments. We do this because
	// some attachments may be identical, and Attachments are not allowed to have identical PayloadIds.
	// We need to keep the duplicate copies of identical payloads because BulkDatas were written into
	// the exports with offsets that expect all attachment segments to exist in the segmented archive.
	auto IntToPayloadId = [](int32 Value)
	{
		alignas(int32) FPayloadId::ByteArray Bytes{};
		static_assert(sizeof(Bytes) >= sizeof(int32), "We are storing an int32 counter in the Bytes array");
		int32* IntView = reinterpret_cast<int32*>(Bytes);
		IntView[0] = Value;
		return FPayloadId(Bytes);
	};

	int32 AttachmentIndex = 1; // 0 is not a valid value for IntToPayloadId
	const FSharedBuffer& ExportsBuffer = PackageStoreWriter->GetHeaderAndExports();
	check(ExportsBuffer.GetSize() > 0); // Header+Exports segment is non-zero in length
	RecordBuilder.AddAttachment(ExportsBuffer, IntToPayloadId(AttachmentIndex++));
	uint64 FileSize = ExportsBuffer.GetSize();
	for (const FSharedBuffer& BulkBuffer : PackageStoreWriter->GetBulkDatas())
	{
		int64 BulkSize = BulkBuffer.GetSize();
		check(BulkSize > 0); // We checked this before adding the Region to the PackageStoreWriter
		RecordBuilder.AddAttachment(BulkBuffer, IntToPayloadId(AttachmentIndex++));
		FileSize += BulkSize;
	}

	TCbWriter<16> MetaData;
	MetaData.BeginObject();
	MetaData << "FileSize" << FileSize;
	MetaData.EndObject();

	RecordBuilder.SetMeta(MetaData.Save().AsObject());
	FRequestOwner Owner(EPriority::Normal);
	Cache.Put({RecordBuilder.Build()}, Package->GetName(), ECachePolicy::Local, Owner);
	Owner.KeepAlive();
	return true;
}

void GetBulkDataList(FName PackageName, UE::DerivedData::IRequestOwner& Owner, TUniqueFunction<void(FSharedBuffer Buffer)>&& Callback)
{
	FString ErrorMessage;
	FPackageDigest PackageDigest;
	bool bIsBlacklisted;
	EPackageDigestResult FindHashResult = GetPackageDigest(*IAssetRegistry::Get(), PackageName, PackageDigest,
		bIsBlacklisted, ErrorMessage);
	switch (FindHashResult)
	{
	case EPackageDigestResult::Success:
		break;
	default:
	{
		Callback(FSharedBuffer());
		return;
	}
	}
	if (bIsBlacklisted)
	{
		Callback(FSharedBuffer());
		return;
	}

	using namespace UE::DerivedData;
	ICache& Cache = GetCache();
	Cache.Get({ GetBulkDataListKey(PackageDigest) },
		WriteToString<128>(PackageName), ECachePolicy::Default, Owner,
		[InnerCallback = MoveTemp(Callback)](FCacheGetCompleteParams&& Params)
		{
			bool bOk = Params.Status == EStatus::Ok;
			InnerCallback(bOk ? Params.Record.GetValue() : FSharedBuffer());
		});
}

void PutBulkDataList(FName PackageName, FSharedBuffer Buffer)
{
	FString ErrorMessage;
	FPackageDigest PackageDigest;
	bool bIsBlacklisted;
	EPackageDigestResult FindHashResult = GetPackageDigest(*IAssetRegistry::Get(), PackageName, PackageDigest,
		bIsBlacklisted, ErrorMessage);
	switch (FindHashResult)
	{
	case EPackageDigestResult::Success:
		break;
	default:
	{
		return;
	}
	}
	if (bIsBlacklisted)
	{
		return;
	}

	using namespace UE::DerivedData;
	ICache& Cache = GetCache();
	FRequestOwner Owner(EPriority::Normal);
	FCacheRecordBuilder RecordBuilder(GetBulkDataListKey(PackageDigest));
	RecordBuilder.SetValue(Buffer);
	Cache.Put({RecordBuilder.Build()}, WriteToString<128>(PackageName), ECachePolicy::Default, Owner);
	Owner.KeepAlive();
}

FIoHash GetPackageAndGuidDigest(FCbWriter& Builder, const FGuid& BulkDataId)
{
	Builder << BulkDataId;
	return Builder.Save().GetRangeHash();
}

void GetBulkDataPayloadId(FName PackageName, const FGuid& BulkDataId, UE::DerivedData::IRequestOwner& Owner,
	TUniqueFunction<void(FSharedBuffer Buffer)>&& Callback)
{
	FString ErrorMessage;
	FCbWriter Builder;
	bool bIsBlacklisted;
	EPackageDigestResult FindHashResult = AppendPackageDigest(*IAssetRegistry::Get(), PackageName, Builder, bIsBlacklisted, ErrorMessage);
	switch (FindHashResult)
	{
	case EPackageDigestResult::Success:
		break;
	default:
	{
		Callback(FSharedBuffer());
		return;
	}
	}
	if (bIsBlacklisted)
	{
		Callback(FSharedBuffer());
		return;
	}
	FIoHash PackageAndGuidDigest = GetPackageAndGuidDigest(Builder, BulkDataId);

	using namespace UE::DerivedData;
	ICache& Cache = GetCache();
	Cache.Get({ GetBulkDataPayloadIdKey(PackageAndGuidDigest) },
		WriteToString<192>(PackageName, TEXT("/"), BulkDataId), ECachePolicy::Default, Owner,
		[InnerCallback = MoveTemp(Callback)](FCacheGetCompleteParams&& Params)
	{
		bool bOk = Params.Status == EStatus::Ok;
		InnerCallback(bOk ? Params.Record.GetValue() : FSharedBuffer());
	});
}

void PutBulkDataPayloadId(FName PackageName, const FGuid& BulkDataId, FSharedBuffer Buffer)
{
	FString ErrorMessage;
	FCbWriter Builder;
	bool bIsBlacklisted;
	EPackageDigestResult FindHashResult = AppendPackageDigest(*IAssetRegistry::Get(), PackageName, Builder, bIsBlacklisted, ErrorMessage);
	switch (FindHashResult)
	{
	case EPackageDigestResult::Success:
		break;
	default:
	{
		return;
	}
	}
	if (bIsBlacklisted)
	{
		return;
	}
	FIoHash PackageAndGuidDigest = GetPackageAndGuidDigest(Builder, BulkDataId);

	using namespace UE::DerivedData;
	ICache& Cache = GetCache();
	FRequestOwner Owner(EPriority::Normal);
	FCacheRecordBuilder RecordBuilder(GetBulkDataPayloadIdKey(PackageAndGuidDigest));
	RecordBuilder.SetValue(Buffer);
	Cache.Put({RecordBuilder.Build()}, WriteToString<128>(PackageName), ECachePolicy::Default, Owner);
	Owner.KeepAlive();
}



}
}
