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
const TCHAR* EditorDomainVersion = TEXT("C217EB656E9B4C04816D3DC0E21901F6");
// Identifier of the CacheBuckets for EditorDomain tables
const TCHAR* EditorDomainPackageBucketName = TEXT("EditorDomainPackage");
const TCHAR* EditorDomainBulkDataListBucketName = TEXT("EditorDomainBulkDataList");
const TCHAR* EditorDomainBulkDataPayloadIdBucketName = TEXT("EditorDomainBulkDataPayloadId");

EPackageDigestResult AppendPackageDigest(FCbWriter& Writer, FString& OutErrorMessage,
	const FAssetPackageData& PackageData, FName PackageName)
{
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
		}
	}
	return EPackageDigestResult::Success;
}

void PrecacheClassDigests(TConstArrayView<FName> ClassNames)
{
	FClassDigestMap& ClassDigests = GetClassDigests();
	TArray<TPair<FName,FClassDigestData>> ClassesToAdd;
	FString ClassNameStr;
	{
		FScopeLock ClassDigestsScopeLock(&ClassDigests.Lock);
		for (FName ClassName : ClassNames)
		{
			if (!ClassDigests.Map.Find(ClassName))
			{
				ClassesToAdd.Emplace_GetRef().Get<0>() = ClassName;
			}
		}
	}
	if (ClassesToAdd.Num())
	{
		FString TargetClassNameString;
		for (int32 Index = 0; Index < ClassesToAdd.Num();)
		{
			FName OldClassFName = ClassesToAdd[Index].Get<0>();
			FClassDigestData& Data = ClassesToAdd[Index].Get<1>();
			OldClassFName.ToString(TargetClassNameString);
			FCoreRedirectObjectName OldClassName(TargetClassNameString);
			FCoreRedirectObjectName NewClassName = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class, OldClassName);
			if (OldClassName != NewClassName)
			{
				TargetClassNameString = NewClassName.ToString();
			}

			if (FPackageName::IsScriptPackage(TargetClassNameString))
			{
				UStruct* Struct = FindObject<UStruct>(nullptr, *TargetClassNameString);
				if (Struct)
				{
					Data.SchemaHash = Struct->GetSchemaHash(false /* bSkipEditorOnly */);
					Data.bNative = true;
					++Index;
				}
				else
				{
					ClassesToAdd.RemoveAtSwap(Index);
				}
			}
			else
			{
				Data.SchemaHash.Reset();
				Data.bNative = false;
				++Index;
			}
		}
		{
			FScopeLock ClassDigestsScopeLock(&ClassDigests.Lock);
			for (TPair<FName,FClassDigestData>& Pair: ClassesToAdd)
			{
				ClassDigests.Map.Add(Pair.Get<0>(), MoveTemp(Pair.Get<1>()));
			}
		}
	}
}

EPackageDigestResult GetPackageDigest(IAssetRegistry& AssetRegistry, FName PackageName,
	FPackageDigest& OutPackageDigest, FString& OutErrorMessage)
{
	FCbWriter Builder;
	EPackageDigestResult Result = AppendPackageDigest(AssetRegistry, PackageName, Builder, OutErrorMessage);
	if (Result == EPackageDigestResult::Success)
	{
		OutPackageDigest = Builder.Save().GetRangeHash();
	}
	return Result;
}

EPackageDigestResult AppendPackageDigest(IAssetRegistry& AssetRegistry, FName PackageName,
	FCbWriter& Builder, FString& OutErrorMessage)
{
	AssetRegistry.WaitForPackage(PackageName.ToString());
	TOptional<FAssetPackageData> PackageData = AssetRegistry.GetAssetPackageDataCopy(PackageName);
	if (!PackageData)
	{
		OutErrorMessage = FString::Printf(TEXT("Package %s does not exist in the AssetRegistry"),
			*PackageName.ToString());
		return EPackageDigestResult::FileDoesNotExist;
	}
	return AppendPackageDigest(Builder, OutErrorMessage, *PackageData, PackageName);
}

UE::DerivedData::FCacheKey GetEditorDomainPackageKey(const FPackageDigest& PackageDigest)
{
	static UE::DerivedData::FCacheBucket EditorDomainPackageCacheBucket =
		GetDerivedDataCacheRef().CreateBucket(EditorDomainPackageBucketName);
	return UE::DerivedData::FCacheKey{EditorDomainPackageCacheBucket, PackageDigest};
}

UE::DerivedData::FCacheKey GetBulkDataListKey(const FPackageDigest& PackageDigest)
{
	static UE::DerivedData::FCacheBucket EditorDomainBulkDataListBucket =
		GetDerivedDataCacheRef().CreateBucket(EditorDomainBulkDataListBucketName);
	return UE::DerivedData::FCacheKey{ EditorDomainBulkDataListBucket, PackageDigest };
}

UE::DerivedData::FCacheKey GetBulkDataPayloadIdKey(const FIoHash& PackageAndGuidDigest)
{
	static UE::DerivedData::FCacheBucket EditorDomainBulkDataPayloadIdBucket =
		GetDerivedDataCacheRef().CreateBucket(EditorDomainBulkDataPayloadIdBucketName);

	return UE::DerivedData::FCacheKey{ EditorDomainBulkDataPayloadIdBucket, PackageAndGuidDigest };
}

UE::DerivedData::FRequest RequestEditorDomainPackage(const FPackagePath& PackagePath,
	const FPackageDigest& PackageDigest, UE::DerivedData::ECachePolicy SkipFlags, UE::DerivedData::EPriority CachePriority,
	UE::DerivedData::FOnCacheGetComplete&& Callback)
{
	UE::DerivedData::ICache& Cache = GetDerivedDataCacheRef();
	checkf((SkipFlags & (~UE::DerivedData::ECachePolicy::SkipData)) == UE::DerivedData::ECachePolicy::None,
		TEXT("SkipFlags should only contain ECachePolicy::Skip* flags"));
	return Cache.Get({ GetEditorDomainPackageKey(PackageDigest) },
		PackagePath.GetDebugName(),
		SkipFlags | UE::DerivedData::ECachePolicy::QueryLocal, CachePriority, MoveTemp(Callback));
}

/** Stores data from SavePackage in accessible fields */
class FMemoryPackageStoreWriter : public IPackageStoreWriter
{
public:
	// IPackageStoreWriter
	virtual void WritePackageData(const FPackageInfo& Info, const FIoBuffer& PackageData, const TArray<FFileRegion>& FileRegions) override;
	virtual void WriteBulkdata(const FBulkDataInfo& Info, const FIoBuffer& BulkData, const TArray<FFileRegion>& FileRegions) override;
	virtual void Flush() override
	{
	}
	virtual void BeginPackage(const FBeginPackageInfo& Info) override;
	virtual void CommitPackage(const FCommitPackageInfo& Info) override
	{
	}

	// IPackageStoreWriter cooking and accessor interface: not implemented in this writer
	bool WriteAdditionalFile(const FAdditionalFileInfo& Info, const FIoBuffer& FileData) override
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
	FIoBuffer GetHeaderAndExports() const { return HeaderAndExports; }
	/**
	 * BulkDatas in this array are views into the FMemoryPackageStoreWriter.
	 * Callers should call MakeOwned if they make copies that need to outlive the FMemoryPackageStoreWriter.
	 */
	const TArray<FIoBuffer>& GetBulkDatas() const { return BulkDataRegions; }

private:
	void SetPackageName(FName InPackageName);

	FIoBuffer HeaderAndExports;
	TArray<FIoBuffer> BulkDataArchives;
	TArray<FIoBuffer> BulkDataRegions;
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
	HeaderAndExports = PackageData;
}

void FMemoryPackageStoreWriter::WriteBulkdata(const FBulkDataInfo& Info, const FIoBuffer& BulkData, const TArray<FFileRegion>& FileRegions)
{
	SetPackageName(Info.PackageName);
	// Info.LooseFilePath is ignored
	// Info.ChunkId is ignored
	checkf(Info.BulkdataType == IPackageStoreWriter::FBulkDataInfo::Standard, TEXT("MemoryPackageStoreWriter does not currently support BulkData types other than Standard."));

	FIoBuffer& BulkDataOwner = BulkDataArchives.Add_GetRef(BulkData);
	const uint8* BulkDataStart = BulkData.Data();
	uint64 BulkDataLen = BulkData.DataSize();
	for (const FFileRegion& FileRegion : FileRegions)
	{
		checkf(FileRegion.Type == EFileRegionType::None, TEXT("FMemoryPackageStoreWriter does not currently support FileRegion types other than None."));
		checkf(FileRegion.Offset + FileRegion.Length <= BulkDataLen, TEXT("FileRegions in WriteBulkdata were outside of the range of the BulkData's size."));
		BulkDataRegions.Add(FIoBuffer(BulkDataStart + FileRegion.Offset, FileRegion.Length, BulkDataOwner));
	}
}

bool TrySavePackage(UPackage* Package)
{
	using namespace UE::DerivedData;

	FString ErrorMessage;
	FPackageDigest PackageDigest;
	EPackageDigestResult FindHashResult = GetPackageDigest(*IAssetRegistry::Get(), Package->GetFName(), PackageDigest,
		ErrorMessage);
	switch (FindHashResult)
	{
	case EPackageDigestResult::Success:
		break;
	default:
		UE_LOG(LogEditorDomain, Warning, TEXT("Could not save package to EditorDomain: %s."), *ErrorMessage)
		return false;
	}

	uint32 SaveFlags = SAVE_NoError // Do not crash the SaveServer on an error
		| SAVE_BulkDataByReference	// EditorDomain saves reference bulkdata from the WorkspaceDomain rather than duplicating it
		| SAVE_Async				// SavePackage support for PackageStoreWriter is only implemented with SAVE_Async
		// EDITOR_DOMAIN_TODO: Add a a save flag that specifies the creation of a deterministic guid
		// | SAVE_KeepGUID;			// Prevent indeterminism by keeping the Guid
		;

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

	// EDITOR_DOMAIN_TODO: Convert FIoBuffer to use FSharedBuffer and refer to the SharedBuffers rather than copying
	FSharedBuffer PackageBuffer;
	{
		TArray64<uint8> TempBytes;
		const FIoBuffer& MainBuffer = PackageStoreWriter->GetHeaderAndExports();
		TempBytes.Append(MainBuffer .Data(), MainBuffer.DataSize());
		for (const FIoBuffer& BulkBuffer : PackageStoreWriter->GetBulkDatas())
		{
			TempBytes.Append(BulkBuffer.Data(), BulkBuffer.DataSize());
		}
		PackageBuffer = MakeSharedBufferFromArray(MoveTemp(TempBytes));
	}

	ICache& Cache = GetDerivedDataCacheRef();
	FCacheRecordBuilder RecordBuilder = Cache.CreateRecord(GetEditorDomainPackageKey(PackageDigest));
	TCbWriter<256> MetaData;
	MetaData.BeginObject();
	MetaData << "FileSize" << PackageBuffer.GetSize();
	MetaData.EndObject();
	RecordBuilder.SetMeta(MetaData.Save().AsObject());
	RecordBuilder.SetValue(PackageBuffer);
	FCacheRecord Record = RecordBuilder.Build();
	Cache.Put(MakeArrayView(&Record, 1), Package->GetName(), ECachePolicy::Local);
	return true;
}

UE::DerivedData::FRequest GetBulkDataList(FName PackageName, TUniqueFunction<void(FSharedBuffer Buffer)>&& Callback)
{
	UE::DerivedData::ICache& Cache = GetDerivedDataCacheRef();

	FString ErrorMessage;
	FPackageDigest PackageDigest;
	EPackageDigestResult FindHashResult = GetPackageDigest(*IAssetRegistry::Get(), PackageName, PackageDigest,
		ErrorMessage);
	switch (FindHashResult)
	{
	case EPackageDigestResult::Success:
		break;
	default:
	{
		Callback(FSharedBuffer());
		return UE::DerivedData::FRequest();
	}
	}

	return Cache.Get({ GetBulkDataListKey(PackageDigest) },
		WriteToString<128>(PackageName), UE::DerivedData::ECachePolicy::Default, UE::DerivedData::EPriority::Low,
		[InnerCallback = MoveTemp(Callback)](UE::DerivedData::FCacheGetCompleteParams&& Params)
		{
			bool bOk = Params.Status == UE::DerivedData::EStatus::Ok;
			InnerCallback(bOk ? Params.Record.GetValue() : FSharedBuffer());
		});
}

void PutBulkDataList(FName PackageName, FSharedBuffer Buffer)
{
	UE::DerivedData::ICache& Cache = GetDerivedDataCacheRef();

	FString ErrorMessage;
	FPackageDigest PackageDigest;
	EPackageDigestResult FindHashResult = GetPackageDigest(*IAssetRegistry::Get(), PackageName, PackageDigest,
		ErrorMessage);
	switch (FindHashResult)
	{
	case EPackageDigestResult::Success:
		break;
	default:
	{
		return;
	}
	}

	UE::DerivedData::FCacheRecordBuilder RecordBuilder = Cache.CreateRecord(GetBulkDataListKey(PackageDigest));
	RecordBuilder.SetValue(Buffer);
	UE::DerivedData::FCacheRecord Record = RecordBuilder.Build();
	Cache.Put(MakeArrayView(&Record, 1), WriteToString<128>(PackageName));
}

FIoHash GetPackageAndGuidDigest(FCbWriter& Builder, const FGuid& BulkDataId)
{
	Builder << BulkDataId;
	return Builder.Save().GetRangeHash();
}

UE::DerivedData::FRequest GetBulkDataPayloadId(FName PackageName, const FGuid& BulkDataId,
	TUniqueFunction<void(FSharedBuffer Buffer)>&& Callback)
{
	UE::DerivedData::ICache& Cache = GetDerivedDataCacheRef();

	FString ErrorMessage;
	FCbWriter Builder;
	EPackageDigestResult FindHashResult = AppendPackageDigest(*IAssetRegistry::Get(), PackageName, Builder, ErrorMessage);
	switch (FindHashResult)
	{
	case EPackageDigestResult::Success:
		break;
	default:
	{
		Callback(FSharedBuffer());
		return UE::DerivedData::FRequest();
	}
	}
	FIoHash PackageAndGuidDigest = GetPackageAndGuidDigest(Builder, BulkDataId);

	return Cache.Get({ GetBulkDataPayloadIdKey(PackageAndGuidDigest) },
		WriteToString<192>(PackageName, TEXT("/"), BulkDataId), UE::DerivedData::ECachePolicy::Default, UE::DerivedData::EPriority::Low,
		[InnerCallback = MoveTemp(Callback)](UE::DerivedData::FCacheGetCompleteParams&& Params)
	{
		bool bOk = Params.Status == UE::DerivedData::EStatus::Ok;
		InnerCallback(bOk ? Params.Record.GetValue() : FSharedBuffer());
	});
}

void PutBulkDataPayloadId(FName PackageName, const FGuid& BulkDataId, FSharedBuffer Buffer)
{
	UE::DerivedData::ICache& Cache = GetDerivedDataCacheRef();

	FString ErrorMessage;
	FCbWriter Builder;
	EPackageDigestResult FindHashResult = AppendPackageDigest(*IAssetRegistry::Get(), PackageName, Builder, ErrorMessage);
	switch (FindHashResult)
	{
	case EPackageDigestResult::Success:
		break;
	default:
	{
		return;
	}
	}
	FIoHash PackageAndGuidDigest = GetPackageAndGuidDigest(Builder, BulkDataId);

	UE::DerivedData::FCacheRecordBuilder RecordBuilder = Cache.CreateRecord(GetBulkDataPayloadIdKey(PackageAndGuidDigest));
	RecordBuilder.SetValue(Buffer);
	UE::DerivedData::FCacheRecord Record = RecordBuilder.Build();
	Cache.Put(MakeArrayView(&Record, 1), WriteToString<128>(PackageName));
}



}
}
