// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Serialization/BulkData2.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/LoadTimeTracker.h"
#include "Serialization/BulkData.h"
#include "UObject/LinkerLoad.h"
#include "UObject/Object.h"

namespace
{
	// TODO: Maybe expose this and start using everywhere?
	static constexpr const TCHAR* InlinedExt = TEXT(".uexp");			// Inlined
	static constexpr const TCHAR* DefaultExt = TEXT(".ubulk");			// Stored in a separate file
	static constexpr const TCHAR* MemoryMappedExt = TEXT(".m.ubulk");	// Stored in a separate file aligned for memory mapping
	static constexpr const TCHAR* OptionalExt = TEXT(".uptnl");			// Stored in a separate file that is optional
}

// TODO: The code in the FileTokenSystem namespace is a temporary system so that FBulkDataBase can hold
// all of it's info about where the data is on disk in a single 8byte value. This can all be removed when
// we switch this over to the new packing system.
namespace FileTokenSystem
{
	struct Data
	{
		FString Filename;
		int64 BulkDataSize;
		int64 BulkDataOffsetInFile;
	};

	TMap<FBulkDataBase::FileToken, Data> TokenDataMap;
	uint64 TokenCounter = 0;
	FRWLock TokenLock;

	FBulkDataBase::FileToken RegisterFileToken( const FString& Filename, uint64 BulkDataSize, uint64 BulkDataOffsetInFile )
	{
		FWriteScopeLock LockForScope(TokenLock);

		Data& Data = TokenDataMap.Add(++TokenCounter);
		Data.Filename = Filename;
		Data.BulkDataSize = BulkDataSize;
		Data.BulkDataOffsetInFile = BulkDataOffsetInFile;

		return TokenCounter;
	}

	void UnregisterFileToken(FBulkDataBase::FileToken ID)
	{
		if (ID != FBulkDataBase::InvalidToken)
		{
			FWriteScopeLock LockForScope(TokenLock);
			TokenDataMap.Remove(ID);
		}
	}

	FBulkDataBase::FileToken CopyFileToken(FBulkDataBase::FileToken ID)
	{
		if (ID != FBulkDataBase::InvalidToken)
		{
			FWriteScopeLock LockForScope(TokenLock);

			Data* Data = TokenDataMap.Find(ID);
			check(Data);

			TokenDataMap.Add(++TokenCounter, *Data);

			return TokenCounter;		
		}
		else
		{
			return FBulkDataBase::InvalidToken;
		}
	}

	Data GetFileData(FBulkDataBase::FileToken ID)
	{
		if (ID == FBulkDataBase::InvalidToken)
		{
			return Data();
		}

		FReadScopeLock LockForScope(TokenLock);
		Data* Data = TokenDataMap.Find(ID);
		check(Data);

		return *Data;
	}

	FString GetFilename(FBulkDataBase::FileToken ID)
	{
		if (ID == FBulkDataBase::InvalidToken)
		{
			return FString();
		}

		FReadScopeLock LockForScope(TokenLock);
		Data* Data = TokenDataMap.Find(ID);
		check(Data);

		return Data->Filename;
	}

	uint64 GetBulkDataSize(FBulkDataBase::FileToken ID)
	{
		if (ID == FBulkDataBase::InvalidToken)
		{
			return 0;
		}
		
		FReadScopeLock LockForScope(TokenLock);
		Data* Data = TokenDataMap.Find(ID);
		check(Data);

		return Data != nullptr ? Data->BulkDataSize : 0;
	}

	uint64 GetBulkDataOffset(FBulkDataBase::FileToken ID)
	{
		if (ID == FBulkDataBase::InvalidToken)
		{
			return 0;
		}

		FReadScopeLock LockForScope(TokenLock);
		Data* Data = TokenDataMap.Find(ID);
		check(Data);

		return Data != nullptr ? Data->BulkDataOffsetInFile : 0;
	}
}

FBulkDataBase::FBulkDataBase(FBulkDataBase&& Other)
	: Token(Other.Token)
	, DataBuffer(Other.DataBuffer)
	, BulkDataFlags(Other.BulkDataFlags)
{
	check(Other.LockStatus == LOCKSTATUS_Unlocked); // Make sure that the other object wasn't inuse

	Other.Token = InvalidToken; // Prevent the other object from unregistering the token
	Other.DataBuffer = nullptr; // Prevent the other object from deleting our data
}

FBulkDataBase& FBulkDataBase::operator=(const FBulkDataBase& Other)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FBulkDataBase::operator="), STAT_UBD_Constructor, STATGROUP_Memory);

	check(LockStatus == LOCKSTATUS_Unlocked);
	check(Other.LockStatus == LOCKSTATUS_Unlocked);

	RemoveBulkData();

	Token = FileTokenSystem::CopyFileToken(Other.Token);

	// Copy token
	BulkDataFlags = Other.BulkDataFlags;

	if (Other.DataBuffer != nullptr)
	{
		const int64 DataSize = Other.GetBulkDataSize();

		AllocateData(DataSize);
		FMemory::Memcpy(DataBuffer, DataBuffer, DataSize);
	}

	return *this;
}

FBulkDataBase::~FBulkDataBase()
{
	check(LockStatus == LOCKSTATUS_Unlocked);

	FreeData();
	FileTokenSystem::UnregisterFileToken(Token);
}

void FBulkDataBase::Serialize(FArchive& Ar, UObject* Owner, int32 /*Index*/, bool bAttemptFileMapping, int32 ElementSize)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FBulkDataBase::Serialize"), STAT_UBD_Serialize, STATGROUP_Memory);

	SCOPED_LOADTIMER(BulkData_Serialize);

#if WITH_EDITOR == 0 && WITH_EDITORONLY_DATA == 0
	check(Ar.IsLoading());				// Only support loading from cooked data!
	check(!GIsEditor);					// The editor path is not supported
	check(GEventDrivenLoaderEnabled);	// We are assuming the EDL path is enabled ( TODO: Might need to remove this check)
	check(LockStatus == LOCKSTATUS_Unlocked);

	if (Ar.IsPersistent() && !Ar.IsObjectReferenceCollector() && !Ar.ShouldSkipBulkData())
	{
		Ar << BulkDataFlags;

		int64 ElementCount = 0;
		int64 BulkDataSizeOnDisk = 0;
		int64 BulkDataSize = 0;
		int64 BulkDataOffsetInFile = 0;

		if (BulkDataFlags & BULKDATA_Size64Bit)
		{
			Ar << ElementCount;
			Ar << BulkDataSizeOnDisk;
		}
		else
		{
			int32 Temp32ByteValue;

			Ar << Temp32ByteValue;
			ElementCount = Temp32ByteValue;

			Ar << Temp32ByteValue;
			BulkDataSizeOnDisk = Temp32ByteValue;
		}

		BulkDataSize = ElementCount * ElementSize;

		Ar << BulkDataOffsetInFile;

		// fix up the file offset, but only if not stored inline
		if (Owner != nullptr && Owner->GetLinker() != nullptr && !IsInlined())
		{
			BulkDataOffsetInFile += Owner->GetLinker()->Summary.BulkDataStartOffset;
		}

		FArchive* CacheableArchive = Ar.GetCacheableArchive();
		if (Ar.IsAllowingLazyLoading() && Owner != nullptr && CacheableArchive != nullptr)
		{
			UPackage* Package = Owner->GetOutermost();
			check(Package != nullptr);

			FLinkerLoad* Linker = FLinkerLoad::FindExistingLinkerForPackage(Package);
			check(Linker != nullptr);

			FString Filename = ConvertFilenameFromFlags(Linker->Filename);

			if (IsInlined())
			{
				// Inline data is already in the archive so serialize it immediately
				AllocateData(BulkDataSize);
				SerializeBulkData(Ar, DataBuffer, BulkDataSize);
			}
			else
			{
				if (IsDuplicateNonOptional())
				{
					FString OptionalFilename = FPaths::ChangeExtension(Filename, OptionalExt);
					if (IFileManager::Get().FileExists(*Filename))
					{
						SerializeDuplicateData(Ar, Owner, BulkDataFlags, BulkDataSizeOnDisk, BulkDataOffsetInFile);
						Filename = OptionalFilename;
					}
					else
					{
						// Skip over the optional data info (can't do a seek because we need to load the flags to work
						// out if we read things as 32bit or 64bit)
						uint32 DummyValue32;
						int64 DummyValue64;
						SerializeDuplicateData(Ar, Owner, DummyValue32, DummyValue64, DummyValue64);

						check(Filename.EndsWith(DefaultExt));
					}			
				}			
			}

			// TODO: Replace with the proper storage solution
			Token = FileTokenSystem::RegisterFileToken(Filename, BulkDataSize, BulkDataOffsetInFile);
		}
		else
		{
			BULKDATA_NOT_IMPLEMENTED_FOR_RUNTIME;
		}
	}
#else
	check(false); // Only implemented for cooked builds!
#endif
}

void* FBulkDataBase::Lock(uint32 LockFlags)
{
	check(LockStatus == LOCKSTATUS_Unlocked);
	
	ForceBulkDataResident(); // Will load 

	if (LockFlags & LOCK_READ_WRITE)
	{
		LockStatus = LOCKSTATUS_ReadWriteLock;
	}
	else if (LockFlags & LOCK_READ_ONLY)
	{
		LockStatus = LOCKSTATUS_ReadOnlyLock;
	}
	else
	{
		UE_LOG(LogSerialization, Fatal, TEXT("Unknown lock flag %i"), LockFlags);
	}

	return DataBuffer;
}

const void* FBulkDataBase::LockReadOnly() const
{
	check(LockStatus == LOCKSTATUS_Unlocked);
	return DataBuffer;
}

void FBulkDataBase::Unlock()
{
	check(LockStatus != LOCKSTATUS_Unlocked);

	LockStatus = LOCKSTATUS_Unlocked;

	// Free pointer if we're guaranteed to only to access the data once.
	if (IsSingleUse())
	{
		FreeData();
	}
}

bool FBulkDataBase::IsLocked() const
{ 
	return LockStatus != LOCKSTATUS_Unlocked;
}

void* FBulkDataBase::Realloc(int64 InElementCount)
{
	BULKDATA_NOT_IMPLEMENTED_FOR_RUNTIME;
	return nullptr;
}

void FBulkDataBase::GetCopy(void** DstBuffer, bool bDiscardInternalCopy)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FUntypedBulkData::GetCopy"), STAT_UBD_GetCopy, STATGROUP_Memory);

	check(LockStatus == LOCKSTATUS_Unlocked);
	check(DstBuffer);

	if (*DstBuffer != nullptr)
	{
		// TODO: Might be worth changing the API so that we can validate that the buffer is large enough?
		if (IsBulkDataLoaded())
		{
			FMemory::Memcpy(*DstBuffer, DataBuffer, GetBulkDataSize());

			if (bDiscardInternalCopy && (CanLoadFromDisk() || IsSingleUse()))
			{
				FreeData();
			}
		}
		else
		{
			LoadDataDirectly(DstBuffer);
		}
	}
	else
	{
		if (IsBulkDataLoaded())
		{
			if (bDiscardInternalCopy && (CanLoadFromDisk() || IsSingleUse()))
			{
				// Since we were going to discard the data anyway we can just hand over ownership to the caller
				::Swap(*DstBuffer, DataBuffer);
			}
			else
			{
				int64 BulkDataSize = GetBulkDataSize();

				*DstBuffer = FMemory::Malloc(BulkDataSize, 0);
				FMemory::Memcpy(*DstBuffer, DataBuffer, GetBulkDataSize());
			}
		}
		else
		{
			*DstBuffer = FMemory::Malloc(GetBulkDataSize(), 0);
			LoadDataDirectly(DstBuffer);
		}
	}
}

void  FBulkDataBase::SetBulkDataFlags(uint32 BulkDataFlagsToSet)
{
	check(!CanLoadFromDisk());	// We only want to allow the editing of flags if the BulkData
								// was dynamically created at runtime, not loaded off disk

	BulkDataFlags |= BulkDataFlagsToSet;
}

void  FBulkDataBase::ResetBulkDataFlags(uint32 BulkDataFlagsToSet)
{
	check(!CanLoadFromDisk());	// We only want to allow the editing of flags if the BulkData
								// was dynamically created at runtime, not loaded off disk

	BulkDataFlags = BulkDataFlagsToSet;
}

void  FBulkDataBase::ClearBulkDataFlags(uint32 BulkDataFlagsToClear)
{ 
	check(!CanLoadFromDisk());	// We only want to allow the editing of flags if the BulkData
								// was dynamically created at runtime, not loaded off disk

	BulkDataFlags &= ~BulkDataFlagsToClear;
}

int64 FBulkDataBase::GetBulkDataSize() const
{
	return FileTokenSystem::GetBulkDataSize(Token);
}

bool FBulkDataBase::IsStoredCompressedOnDisk() const
{
	return (GetBulkDataFlags() & BULKDATA_SerializeCompressed) != 0;
}

FName FBulkDataBase::GetDecompressionFormat() const
{
	return (BulkDataFlags & BULKDATA_SerializeCompressedZLIB) ? NAME_Zlib : NAME_None;
}

bool FBulkDataBase::IsAvailableForUse() const 
{ 
	return (GetBulkDataFlags() & BULKDATA_Unused) != 0;
}

bool FBulkDataBase::IsDuplicateNonOptional() const
{
	return (GetBulkDataFlags() & BULKDATA_DuplicateNonOptionalPayload) != 0;
}

bool FBulkDataBase::IsOptional() const
{ 
	return (GetBulkDataFlags() & BULKDATA_OptionalPayload) != 0;
}

bool FBulkDataBase::IsInlined() const
{
	return	(GetBulkDataFlags() & BULKDATA_PayloadAtEndOfFile) == 0;
}

bool FBulkDataBase::InSeperateFile() const
{
	return	(GetBulkDataFlags() & BULKDATA_PayloadInSeperateFile) != 0;
}

bool FBulkDataBase::IsSingleUse() const
{
	return (BulkDataFlags & BULKDATA_SingleUse) != 0;
}

bool FBulkDataBase::IsMemoryMapped() const
{
	return (BulkDataFlags & BULKDATA_MemoryMappedPayload) != 0;
}

FBulkDataIORequest* FBulkDataBase::CreateStreamingRequest(EAsyncIOPriorityAndFlags Priority, FAsyncFileCallBack* CompleteCallback, uint8* UserSuppliedMemory) const
{
	const int64 DataSize = GetBulkDataSize();

	return CreateStreamingRequest(0, DataSize, Priority, CompleteCallback, UserSuppliedMemory);
}

FBulkDataIORequest* FBulkDataBase::CreateStreamingRequest(int64 OffsetInBulkData, int64 BytesToRead, EAsyncIOPriorityAndFlags Priority, FAsyncFileCallBack* CompleteCallback, uint8* UserSuppliedMemory) const
{
#if 1
	FileTokenSystem::Data FileData = FileTokenSystem::GetFileData(Token);

	check(FileData.Filename.IsEmpty() == false);

	UE_CLOG(IsStoredCompressedOnDisk(), LogSerialization, Fatal, TEXT("Package level compression is no longer supported (%s)."), *FileData.Filename);
	UE_CLOG(FileData.BulkDataSize <= 0, LogSerialization, Error, TEXT("(%s) has invalid bulk data size."), *FileData.Filename);

	IAsyncReadFileHandle* IORequestHandle = FPlatformFileManager::Get().GetPlatformFile().OpenAsyncRead(*FileData.Filename);
	check(IORequestHandle); // this generally cannot fail because it is async

	if (IORequestHandle == nullptr)
	{
		return nullptr;
	}

	const int64 OffsetInFile = FileData.BulkDataOffsetInFile + OffsetInBulkData;

	IAsyncReadRequest* ReadRequest = IORequestHandle->ReadRequest(OffsetInFile, BytesToRead, Priority, CompleteCallback, UserSuppliedMemory);
	if (ReadRequest != nullptr)
	{
		return new FBulkDataIORequest(IORequestHandle, ReadRequest, BytesToRead);
	}
	else
	{
		delete IORequestHandle;
		return nullptr;
	}
#else
	return nullptr;
#endif
}

void FBulkDataBase::ForceBulkDataResident()
{
	if (!IsBulkDataLoaded())
	{
		LoadDataDirectly(&DataBuffer);
	}
}

FOwnedBulkDataPtr* FBulkDataBase::StealFileMapping()
{
	check(LockStatus == LOCKSTATUS_Unlocked);

	FOwnedBulkDataPtr* Result = new FOwnedBulkDataPtr(DataBuffer);

	// We are giving ownership of the data to the caller so we just null out the pointer.
	DataBuffer = nullptr;

	return Result;
}

void FBulkDataBase::RemoveBulkData()
{
	check(LockStatus == LOCKSTATUS_Unlocked);

	FreeData();

	FileTokenSystem::UnregisterFileToken(Token);
	Token = InvalidToken;

	BulkDataFlags = 0;

}

int64 FBulkDataBase::GetBulkDataOffsetInFile() const
{
	return FileTokenSystem::GetBulkDataOffset(Token);
}

FString FBulkDataBase::GetFilename() const
{
	return FileTokenSystem::GetFilename(Token);
}

void FBulkDataBase::LoadDataDirectly(void** DstBuffer)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FBulkDataBase::LoadDataDirectly"), STAT_UBD_LoadDataDirectly, STATGROUP_Memory);
	check(CanLoadFromDisk());

	FileTokenSystem::Data FileData = FileTokenSystem::GetFileData(Token);

	// In the older code path if the bulkdata was not in a separate file we could just serialize from the package instead, but the new system
	// does not keep a reference to the pack so we cannot do that.
	// TODO: Maybe add a check for this condition?

	check(!IsInlined());	// TODO: Currently inline data will have the wrong BulkDataOffsetInFile (need to subtract the size of the original
							// .uasset or .umap file from it but we don't have access to that filepath at this point and doing it during
							// ::Serialize is too slow!)

	// If the data is inlined then we already loaded is during ::Serialize, this warning should help track cases where data is being discarded then re-requested.
	UE_CLOG(IsInlined(), LogSerialization, Warning, TEXT("Reloading inlined bulk data directly from disk, consider not discarding it in the first place. Filename: '%s'."), *FileData.Filename);

	FArchive* Ar = IFileManager::Get().CreateFileReader(*FileData.Filename, FILEREAD_Silent);
	checkf(Ar != nullptr, TEXT("Failed to open the file to load bulk data from. Filename: '%s'."), *FileData.Filename);

	// Seek to the beginning of the bulk data in the file.
	Ar->Seek(FileData.BulkDataOffsetInFile);

	if (*DstBuffer == nullptr)
	{
		*DstBuffer = FMemory::Malloc(FileData.BulkDataSize, 0);
	}

	SerializeBulkData(*Ar, *DstBuffer, FileData.BulkDataSize);

	delete Ar;
}

void FBulkDataBase::SerializeDuplicateData(FArchive& Ar, UObject* Owner, uint32& OutBulkDataFlags, int64& OutBulkDataSizeOnDisk, int64& OutBulkDataOffsetInFile)
{
	Ar << OutBulkDataFlags;

	if (OutBulkDataFlags & BULKDATA_Size64Bit)
	{
		Ar << OutBulkDataSizeOnDisk;
	}
	else
	{
		int32 Temp32ByteValue;
		Ar << Temp32ByteValue;

		OutBulkDataSizeOnDisk = Temp32ByteValue;
	}

	Ar << OutBulkDataOffsetInFile;

	// fix up the file offset, but only if not stored inline
	if (Owner != nullptr && Owner->GetLinker() != nullptr)
	{
		OutBulkDataOffsetInFile += Owner->GetLinker()->Summary.BulkDataStartOffset;
	}
}

void FBulkDataBase::SerializeBulkData(FArchive& Ar, void* DstBuffer, int64 DataLength)
{	
	check(Ar.IsLoading()); // Currently only support loading

	if (IsAvailableForUse()) // skip serializing of unused data
	{
		return;
	}

	// Skip serialization for bulk data of zero length
	if (DataLength == 0)
	{
		return;
	}

	check(DstBuffer != nullptr);

	if (IsStoredCompressedOnDisk())
	{
		Ar.SerializeCompressed(DstBuffer, DataLength, GetDecompressionFormat(), COMPRESS_NoFlags, false);
	}
	// Uncompressed/ regular serialization.
	else
	{
		Ar.Serialize(DstBuffer, DataLength);
	}
}

void FBulkDataBase::AllocateData(SIZE_T SizeInBytes)
{
	DataBuffer = FMemory::Realloc(DataBuffer, SizeInBytes, DEFAULT_ALIGNMENT);
}

void FBulkDataBase::FreeData()
{
	FMemory::Free(DataBuffer);
	DataBuffer = nullptr;
}

FString FBulkDataBase::ConvertFilenameFromFlags(const FString& Filename)
{
	if (IsInlined())
	{
		return FPaths::ChangeExtension(Filename, InlinedExt);
	}
	else if (IsOptional())
	{
		return FPaths::ChangeExtension(Filename, OptionalExt);
	}
	else if (IsMemoryMapped())
	{
		return FPaths::ChangeExtension(Filename, MemoryMappedExt);
	}
	else
	{
		return FPaths::ChangeExtension(Filename, DefaultExt);
	}
}
