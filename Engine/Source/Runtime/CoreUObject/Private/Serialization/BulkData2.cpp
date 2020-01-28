// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/BulkData2.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/LoadTimeTracker.h"
#include "Serialization/BulkData.h"
#include "UObject/LinkerLoad.h"
#include "UObject/Object.h"
#include "UObject/Package.h"
#include "IO/IoDispatcher.h"

DEFINE_LOG_CATEGORY_STATIC(LogBulkDataRuntime, Log, All);

// If set to 0 then the loose file fallback will be used even if the -ZenLoader command line flag is present.
#define ENABLE_IO_DISPATCHER 1

// If set to 0 then we will pretend that optional data does not exist, useful for testing.
#define ALLOW_OPTIONAL_DATA 1

namespace
{
	// TODO: Maybe expose this and start using everywhere?
	static const FString InlinedExt = TEXT(".uexp");			// Stored in the export data
	static const FString DefaultExt = TEXT(".ubulk");			// Stored in a separate file
	static const FString MemoryMappedExt = TEXT(".m.ubulk");	// Stored in a separate file aligned for memory mapping
	static const FString OptionalExt = TEXT(".uptnl");			// Stored in a separate file that is optional

	const uint16 InvalidBulkDataIndex = ~uint16(0);

	FORCEINLINE bool IsIoDispatcherEnabled()
	{
#if ENABLE_IO_DISPATCHER
		return FIoDispatcher::IsInitialized();
#else
		return false;
#endif
	}
}

// TODO: The code in the FileTokenSystem namespace is a temporary system so that FBulkDataBase can hold
// all of it's info about where the data is on disk in a single 8byte value. This can all be removed when
// we switch this over to the new packing system.
namespace FileTokenSystem
{
	struct Data
	{
		int64 BulkDataOffsetInFile;
		FString PackageHeaderFilename;
	};

	// Internal to the FileTokenSystem namespace
	namespace
	{
		struct InternalData
		{
			FName PackageName;
			int64 BulkDataOffsetInFile;			
		};

		struct StringData
		{
			FString Filename;
			uint16 RefCount;
		};

		/**
		* Provides a ref counted PackageName->Filename look up table.
		*/
		class FStringTable
		{
		public:
			void Add(const FName& PackageName, const FString& Filename)
			{
				if (StringData* ExistingEntry = Table.Find(PackageName))
				{
					ExistingEntry->RefCount++;
				}
				else
				{
					StringData& NewEntry = Table.Emplace(PackageName);
					NewEntry.Filename = Filename;
					NewEntry.RefCount = 1;
				}
			}

			bool Remove(const FName& PackageName)
			{
				if (StringData* ExistingEntry = Table.Find(PackageName))
				{
					if (--ExistingEntry->RefCount == 0)
					{
						Table.Remove(PackageName);
						return true;
					}
				}

				return false;
			}

			void IncRef(const FName& PackageName)
			{
				if (StringData* ExistingEntry = Table.Find(PackageName))
				{
					ExistingEntry->RefCount++;
				}
			}

			const FString& Resolve(const FName& PackageName)
			{
				return Table.Find(PackageName)->Filename;
			}

			int32 Num() const
			{
				return Table.Num();
			}

		private:
			TMap<FName, StringData> Table;
		};
	}

	FStringTable StringTable;
	TSparseArray<InternalData> TokenData;

	FRWLock TokenLock;

	FBulkDataBase::FileToken RegisterFileToken( const FName& PackageName, const FString& Filename, uint64 BulkDataOffsetInFile )
	{
		FWriteScopeLock LockForScope(TokenLock);

		StringTable.Add(PackageName, Filename);

		InternalData Data;
		Data.PackageName = PackageName;
		Data.BulkDataOffsetInFile = BulkDataOffsetInFile;

		FBulkDataBase::FileToken FileToken  = TokenData.Add(Data);
		return FileToken;
	}

	void UnregisterFileToken(FBulkDataBase::FileToken ID)
	{
		if (ID != FBulkDataBase::InvalidToken)
		{
			FWriteScopeLock LockForScope(TokenLock);

			StringTable.Remove(TokenData[ID].PackageName);
			TokenData.RemoveAt(ID);

			check(StringTable.Num() <= TokenData.Num());
		}
	}

	FBulkDataBase::FileToken CopyFileToken(FBulkDataBase::FileToken ID)
	{
		if (ID != FBulkDataBase::InvalidToken)
		{
			FWriteScopeLock LockForScope(TokenLock);

			FSparseArrayAllocationInfo AllocInfo = TokenData.AddUninitialized();
			
			const InternalData& OriginalData = TokenData[ID];
			InternalData& NewData = TokenData[AllocInfo.Index];

			NewData.PackageName = OriginalData.PackageName;
			NewData.BulkDataOffsetInFile = OriginalData.BulkDataOffsetInFile;

			StringTable.IncRef(NewData.PackageName);

			return AllocInfo.Index;
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
		const InternalData& DataSrc = TokenData[ID];

		Data Output;
		Output.BulkDataOffsetInFile = DataSrc.BulkDataOffsetInFile;
		Output.PackageHeaderFilename = StringTable.Resolve(DataSrc.PackageName);

		return Output;
	}

	FString GetFilename(FBulkDataBase::FileToken ID)
	{
		if (ID == FBulkDataBase::InvalidToken)
		{
			return FString();
		}

		FReadScopeLock LockForScope(TokenLock);
		return StringTable.Resolve(TokenData[ID].PackageName);
	}

	uint64 GetBulkDataOffset(FBulkDataBase::FileToken ID)
	{
		if (ID == FBulkDataBase::InvalidToken)
		{
			return 0;
		}

		FReadScopeLock LockForScope(TokenLock);
		return TokenData[ID].BulkDataOffsetInFile;
	}
}

FIoDispatcher* FBulkDataBase::IoDispatcher = nullptr;

class FSizeChunkIdRequest : public IAsyncReadRequest
{
public:
	FSizeChunkIdRequest(const FIoChunkId& ChunkId, FAsyncFileCallBack* Callback)
		: IAsyncReadRequest(Callback, true, nullptr)
	{
		TIoStatusOr<uint64> Result = FBulkDataBase::GetIoDispatcher()->GetSizeForChunk(ChunkId);
		if (Result.IsOk())
		{
			Size = Result.ValueOrDie();
		}

		SetComplete();
	}

	virtual ~FSizeChunkIdRequest() = default;

private:

	virtual void WaitCompletionImpl(float TimeLimitSeconds) override
	{
		// No need to wait as the work is done in the constructor
	}

	virtual void CancelImpl() override
	{
		// No point canceling as the work is done in the constructor
	}
};

class FReadChunkIdRequest : public IAsyncReadRequest
{
public:
	FReadChunkIdRequest(const FIoChunkId& InChunkId, FAsyncFileCallBack* InCallback, uint8* InUserSuppliedMemory, int64 InOffset, int64 InBytesToRead)
		: IAsyncReadRequest(InCallback, false, InUserSuppliedMemory)
	{
		// Because IAsyncReadRequest can return ownership of the target memory buffer in the form
		// of a raw pointer we must pass our own memory buffer to the FIoDispatcher otherwise the 
		// buffer that will be returned cannot have it's lifetime managed correctly.
		if (InUserSuppliedMemory == nullptr)
		{
			Memory = (uint8*)FMemory::Malloc(InBytesToRead);
		}

		DoneEvent = FPlatformProcess::GetSynchEventFromPool(true);
		DoneEvent->Reset();
		
		FIoReadOptions Options(InOffset, InBytesToRead);
		Options.SetTargetVa(Memory);

		auto OnRequestLoaded = [this](TIoStatusOr<FIoBuffer> Result)
		{
			SetComplete();
			ReleaseEvent(true);
		};

		FBulkDataBase::GetIoDispatcher()->ReadWithCallback(InChunkId, Options, OnRequestLoaded);
	}

	virtual ~FReadChunkIdRequest()
	{
		ReleaseEvent(false);

		// Free memory if the request allocated it (although if the user accessed the memory after
		// reading then they will have taken ownership of it anyway, and if they didn't access the
		// memory then why did we read it in the first place?)
		if (Memory != nullptr && !bUserSuppliedMemory)
		{
			FMemory::Free(Memory);
		}

		// ~IAsyncReadRequest expects Memory to be nullptr, even if the memory was user supplied
		Memory = nullptr;
	}

protected:

	virtual void WaitCompletionImpl(float TimeLimitSeconds) override
	{
		if (!PollCompletion())
		{
			uint32 TimeLimitMilliseconds = TimeLimitSeconds <= 0.0f ? (uint32)(TimeLimitSeconds * 1000.0f) : MAX_uint32;
			DoneEvent->Wait(TimeLimitMilliseconds);
		}
	}

	virtual void CancelImpl() override
	{
		bCanceled = true;
		SetComplete();

		DoneEvent->Trigger();
		ReleaseEvent(true);
	}

	void ReleaseEvent(bool bShouldTrigger)
	{
		if (DoneEvent != nullptr)
		{
			if (bShouldTrigger)
			{
				DoneEvent->Trigger();
			}

			FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
			DoneEvent = nullptr;
		}
	}

	FIoChunkId ChunkId;
	FEvent* DoneEvent;
};

class FAsyncReadChunkIdHandle : public IAsyncReadFileHandle
{
public:
	FAsyncReadChunkIdHandle(const FIoChunkId& InChunkID) 
		: ChunkID(InChunkID)
	{

	}

	virtual ~FAsyncReadChunkIdHandle() = default;

	virtual IAsyncReadRequest* SizeRequest(FAsyncFileCallBack* CompleteCallback = nullptr) override
	{
		return new FSizeChunkIdRequest(ChunkID, CompleteCallback);
	}

	virtual IAsyncReadRequest* ReadRequest(int64 Offset, int64 BytesToRead, EAsyncIOPriorityAndFlags PriorityAndFlags = AIOP_Normal, FAsyncFileCallBack* CompleteCallback = nullptr, uint8* UserSuppliedMemory = nullptr) override
	{
		return new FReadChunkIdRequest(ChunkID, CompleteCallback, UserSuppliedMemory, Offset, BytesToRead);
	}
private:
	FIoChunkId ChunkID;
};

class FBulkDataIoDispatcherRequest : public IBulkDataIORequest
{
public:
	using ChunkIdArray = TArray<FIoChunkId, TInlineAllocator<8>>;

public:
	FBulkDataIoDispatcherRequest(const FIoChunkId& InChunkID, int64 InOffsetInBulkData, int64 InBytesToRead, FBulkDataIORequestCallBack* InCompleteCallback, uint8* InUserSuppliedMemory)
		: UserSuppliedMemory(InUserSuppliedMemory)
	{
		RequestArray.Push({ InChunkID, (uint64)InOffsetInBulkData , (uint64)InBytesToRead });

		if (InCompleteCallback != nullptr)
		{
			CompleteCallback = *InCompleteCallback;
		}
	}

	FBulkDataIoDispatcherRequest(const ChunkIdArray& ChunkIDs, FBulkDataIORequestCallBack* InCompleteCallback)
		: UserSuppliedMemory(nullptr)
	{
		for (const FIoChunkId& ChunkId : ChunkIDs)
		{
			RequestArray.Push({ChunkId, 0, (uint64)INDEX_NONE});
		}

		if (InCompleteCallback != nullptr)
		{
			CompleteCallback = *InCompleteCallback;
		}
	}

	virtual ~FBulkDataIoDispatcherRequest()
	{
		if (Task != nullptr)
		{
			Task->EnsureCompletion();
			delete Task;
		}

		// Free the data is no caller has taken ownership of it and it was allocated by FBulkDataIoDispatcherRequest
		if (UserSuppliedMemory == nullptr)
		{
			FMemory::Free(DataResult);
			DataResult = nullptr;
		}
	}

	void StartAsyncWork()
	{
		check(Task == nullptr);

		bIsCompleted = false;
		FPlatformMisc::MemoryBarrier();

		if (FPlatformProcess::SupportsMultithreading())
		{
			Task = new FAsyncTask<FBulkDataIoDispatcherRequestWorker>(*this);
			Task->StartBackgroundTask(GBackgroundPriorityThreadPool);
		}
		else
		{
			// No multi threading support so just process immediately
			DoWork();
		}
	}

	virtual bool PollCompletion() const override
	{
		return bIsCompleted;
	}

	virtual bool WaitCompletion(float TimeLimitSeconds) const override
	{
		// Note that currently we do not get events from the FIoDispatcher, so we just
		// have a basic implementation.
		// We only have one use case for a time limited wait, every other use case is 
		// supposed to be fully blocking so ideally we can eliminate the single use case 
		// and just change this code entirely.
		if (!bIsCompleted)
		{
			if (TimeLimitSeconds > 0.0f)
			{
				FPlatformProcess::Sleep(TimeLimitSeconds);
			}
			else
			{
				while (!bIsCompleted)
				{
					FPlatformProcess::Sleep(0.0f);
				}
			}
		}

		return bIsCompleted;
	}

	virtual uint8* GetReadResults() override
	{
		if (bIsCompleted && !bIsCanceled)
		{
			uint8* Result = DataResult;
			DataResult = nullptr;

			return Result;
		}
		else
		{
			return nullptr;
		}	
	}

	virtual int64 GetSize() const override
	{
		if (bIsCompleted && !bIsCanceled)
		{
			return SizeResult;
		}
		else
		{
			return INDEX_NONE;
		}
	}

	virtual void Cancel() override
	{
		if (!bIsCanceled)
		{
			bIsCanceled = true;
			FPlatformMisc::MemoryBarrier();
			// TODO: Send to IoDispatcher
		}
	}

private:

	void DoWork()
	{
		FIoReadOptions IoOptions;

		// TODO: FIoReadOptions is ignored at the moment
		//IoOptions.SetRange(OffsetInBulkData, BytesToRead);
		//IoOptions.SetTargetVa((uint64)UserSuppliedMemory);

		FIoBatch NewBatch = FBulkDataBase::GetIoDispatcher()->NewBatch();
		for (Request& Request : RequestArray)
		{
			Request.IoRequest = NewBatch.Read(Request.ChunkId, IoOptions);
		}
		NewBatch.Issue();

		// Wait until the request is completed
		while (!AreRequestsCompleted())
		{
			FPlatformProcess::Sleep(0.0f);
		}

		SizeResult = GetTotalBufferSize();

		uint8* DstBuffer = UserSuppliedMemory != nullptr ? UserSuppliedMemory : (uint8*)FMemory::Malloc(SizeResult);
		DataResult = DstBuffer;

		for (const Request& Request : RequestArray)
		{
			TIoStatusOr<FIoBuffer> Result = Request.IoRequest.GetResult();
			check(Result.IsOk());

			const FIoBuffer& IoBuffer = Result.ValueOrDie();
			uint64 BytesToRead = Request.BytesToRead == INDEX_NONE ? IoBuffer.DataSize() : Request.BytesToRead;
		
			FMemory::Memcpy(DstBuffer, IoBuffer.Data() + Request.OffsetInBulkData, BytesToRead);
			DstBuffer += BytesToRead;
		}
		
		FBulkDataBase::GetIoDispatcher()->FreeBatch(NewBatch);

		bIsCompleted = true;
		FPlatformMisc::MemoryBarrier();

		if (CompleteCallback)
		{
			CompleteCallback(bIsCanceled, this);
		}	
	}

	class FBulkDataIoDispatcherRequestWorker : public FNonAbandonableTask
	{
		FBulkDataIoDispatcherRequest& IoRequest;
	public:
		FBulkDataIoDispatcherRequestWorker(FBulkDataIoDispatcherRequest& InIoRequest)
			: IoRequest(InIoRequest)
		{
		}

		void DoWork()
		{
			IoRequest.DoWork();
		}

		FORCEINLINE TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(FBulkDataIoDispatcherRequestWorker, STATGROUP_ThreadPoolAsyncTasks);
		}
	};

private:
	struct Request
	{
		FIoChunkId ChunkId;
		uint64 OffsetInBulkData;
		uint64 BytesToRead;

		FIoRequest IoRequest;
	};

	bool AreRequestsCompleted() const
	{
		for (const Request& Req : RequestArray)
		{
			if (!Req.IoRequest.Status().IsCompleted())
			{
				return false;
			}
		}

		return true;
	}

	int64 GetTotalBufferSize()  
	{
		uint64 Size = 0;
		for (Request& Req : RequestArray)
		{
			TIoStatusOr<FIoBuffer> Result = Req.IoRequest.GetResult();
			Size += Result.ValueOrDie().DataSize();
		}

		return Size;
	}

	TArray<Request, TInlineAllocator<8>> RequestArray;

	FBulkDataIORequestCallBack CompleteCallback;
	uint8* UserSuppliedMemory = nullptr;

	uint8* DataResult = nullptr;
	int64 SizeResult = 0;

	// Task related members
	FAsyncTask<FBulkDataIoDispatcherRequestWorker>* Task = nullptr;
	bool bIsCompleted = false;
	bool bIsCanceled = false;
};

FBulkDataBase::FBulkDataBase(FBulkDataBase&& Other)
	: ChunkID(Other.ChunkID) // Copies the entire union
	, DataBuffer(Other.DataBuffer)
	, BulkDataFlags(Other.BulkDataFlags)
{
	check(Other.LockStatus == LOCKSTATUS_Unlocked); // Make sure that the other object wasn't inuse

	if (!Other.IsUsingIODispatcher())
	{
		Other.Fallback.Token = InvalidToken; // Prevent the other object from unregistering the token
	}	
	Other.DataBuffer = nullptr;	// Prevent the other object from deleting our data
}

FBulkDataBase& FBulkDataBase::operator=(const FBulkDataBase& Other)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FBulkDataBase::operator="), STAT_UBD_Constructor, STATGROUP_Memory);

	check(LockStatus == LOCKSTATUS_Unlocked);
	check(Other.LockStatus == LOCKSTATUS_Unlocked);

	RemoveBulkData();

	Fallback.Token = FileTokenSystem::CopyFileToken(Other.Fallback.Token);

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
	if (!IsUsingIODispatcher())
	{
		FileTokenSystem::UnregisterFileToken(Fallback.Token);
	}	
}

void FBulkDataBase::Serialize(FArchive& Ar, UObject* Owner, int32 /*Index*/, bool bAttemptFileMapping, int32 ElementSize)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FBulkDataBase::Serialize"), STAT_UBD_Serialize, STATGROUP_Memory);

	SCOPED_LOADTIMER(BulkData_Serialize);

#if WITH_EDITOR == 0 && WITH_EDITORONLY_DATA == 0
	if (Ar.IsPersistent() && !Ar.IsObjectReferenceCollector() && !Ar.ShouldSkipBulkData())
	{
		check(Ar.IsLoading());				// Only support loading from cooked data!
		check(!GIsEditor);					// The editor path is not supported
		check(LockStatus == LOCKSTATUS_Unlocked);

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

		const bool bUseIoDispatcher = IsIoDispatcherEnabled();

		if ((BulkDataFlags & BULKDATA_BadDataVersion) != 0)
		{
			uint16 DummyValue;
			Ar << DummyValue;
		}

		// Assuming that Owner/Package/Linker are all valid, the old BulkData system would
		// generally fail if any of these were nullptr but had plenty of inconsistent checks
		// scattered throughout.
		check(Owner != nullptr);
		const UPackage* Package = Owner->GetOutermost();
		check(Package != nullptr);

		if (!IsInlined() && bUseIoDispatcher)
		{
			const EIoChunkType Type = IsOptional() ? EIoChunkType::OptionalBulkData : EIoChunkType::BulkData;

			const int64 BulkDataID = BulkDataSize > 0 ? BulkDataOffsetInFile : TNumericLimits<uint64>::Max();
			ChunkID = CreateBulkdataChunkId(Package->GetPackageId().ToIndex(), BulkDataID, Type);

			BulkDataFlags |= BULKDATA_UsesIoDispatcher; // Indicates that this BulkData should use the FIoChunkId rather than a filename
		}
		else
		{
			// Invalidate the Token and then set the BulkDataSize for fast retrieval
			Fallback.Token = InvalidToken;
			Fallback.BulkDataSize = BulkDataSize;
		}

		FName PackageName;
		const FString* Filename = nullptr;
		const FLinkerLoad* Linker = nullptr;

		if (bUseIoDispatcher == false)
		{
			Linker = FLinkerLoad::FindExistingLinkerForPackage(Package);
			
			if (Linker != nullptr)
			{
				Filename = &Linker->Filename;
				PackageName = Package->FileName;
			}
		}

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
				ProcessDuplicateData(Ar, Package, Filename, BulkDataSizeOnDisk, BulkDataOffsetInFile);
			}

			// Fix up the file offset if we have a linker (if we do not then we will be loading via FIoDispatcher anyway)
			if (Linker != nullptr)
			{
				BulkDataOffsetInFile += Linker->Summary.BulkDataStartOffset;
			}

			// If the archive does not support lazy loading and the data is not in a different file then we have to load 
			// the data from the archive immediately as we won't get another chance.
			if (!Ar.IsAllowingLazyLoading() && !IsInSeperateFile())
			{
				const int64 CurrentArchiveOffset = Ar.Tell();
				Ar.Seek(BulkDataOffsetInFile);

				AllocateData(BulkDataSize);
				SerializeBulkData(Ar, DataBuffer, BulkDataSize);

				Ar.Seek(CurrentArchiveOffset); // Return back to the original point in the archive so future serialization can continue
			}
		}

		// If we are not using the FIoDispatcher and we have a filename then we need to make sure we can retrieve it later!
		if (bUseIoDispatcher == false && Filename != nullptr)
		{
			Fallback.Token = FileTokenSystem::RegisterFileToken(PackageName, *Filename, BulkDataOffsetInFile);
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
	LockStatus = LOCKSTATUS_ReadOnlyLock;
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

void* FBulkDataBase::Realloc(int64 SizeInBytes)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FBulkDataBase::Realloc"), STAT_UBD_Realloc, STATGROUP_Memory);

	check(LockStatus == LOCKSTATUS_ReadWriteLock);
	checkf(!CanLoadFromDisk(), TEXT("Cannot re-allocate a FBulkDataBase object that represents a file on disk!"));

	AllocateData(SizeInBytes);

	check(!IsUsingIODispatcher());	// This case should get caught above but if someone tries to change that in the
									// future then this check is a reminder that we need to handle the IoDispatcher 
									// vs fallback case.

	Fallback.BulkDataSize = SizeInBytes;

	return DataBuffer;
}

void FBulkDataBase::GetCopy(void** DstBuffer, bool bDiscardInternalCopy)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FBulkDataBase::GetCopy"), STAT_UBD_GetCopy, STATGROUP_Memory);

	check(LockStatus == LOCKSTATUS_Unlocked);
	check(DstBuffer);

	if (*DstBuffer != nullptr)
	{
		// TODO: Might be worth changing the API so that we can validate that the buffer is large enough?
		if (IsBulkDataLoaded())
		{
			FMemory::Memcpy(*DstBuffer, DataBuffer, GetBulkDataSize());

			if (bDiscardInternalCopy && CanDiscardInternalData())
			{
				UE_LOG(LogSerialization, Warning, TEXT("FBulkDataBase::GetCopy both copied and discarded it's data, passing in an empty pointer would avoid an extra allocate and memcpy!"));
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
			if (bDiscardInternalCopy && CanDiscardInternalData())
			{
				// Since we were going to discard the data anyway we can just hand over ownership to the caller
				::Swap(*DstBuffer, DataBuffer);
			}
			else
			{
				const int64 BulkDataSize = GetBulkDataSize();

				*DstBuffer = FMemory::Malloc(BulkDataSize, 0);
				FMemory::Memcpy(*DstBuffer, DataBuffer, BulkDataSize);
			}
		}
		else
		{
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
	if (IsUsingIODispatcher())
	{
		TIoStatusOr<uint64> Result = IoDispatcher->GetSizeForChunk(ChunkID);
		return Result.ValueOrDie(); // TODO: Consider logging errors instead of relying on ::ValueOrDie
	}
	else
	{
		return Fallback.BulkDataSize;
	}	
}

bool FBulkDataBase::CanLoadFromDisk() const 
{ 
	// If this BulkData is using the IoDispatcher then it can load from disk 
	if (IsUsingIODispatcher())
	{
		return true;
	}

	// If this BulkData has a fallback token then it can find it's filepath and load from disk
	if (Fallback.Token != InvalidToken)
	{
		return true;
	}

	return false;
}

bool FBulkDataBase::DoesExist() const
{
#if ALLOW_OPTIONAL_DATA
	if (!IsUsingIODispatcher())
	{
		FString Filename = FileTokenSystem::GetFilename(Fallback.Token);
		Filename = ConvertFilenameFromFlags(Filename);

		return IFileManager::Get().FileExists(*Filename);
	}
	else
	{
		return IoDispatcher->DoesChunkExist(ChunkID);
	}
#else
	return false;
#endif
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

bool FBulkDataBase::IsInSeperateFile() const
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

bool FBulkDataBase::IsUsingIODispatcher() const
{
	return (BulkDataFlags & BULKDATA_UsesIoDispatcher) != 0;
}

IAsyncReadFileHandle* FBulkDataBase::OpenAsyncReadHandle() const
{
	if (IsUsingIODispatcher())
	{
		return new FAsyncReadChunkIdHandle(ChunkID);
	}
	else
	{
		return FPlatformFileManager::Get().GetPlatformFile().OpenAsyncRead(*GetFilename());
	}
}

IBulkDataIORequest* FBulkDataBase::CreateStreamingRequest(EAsyncIOPriorityAndFlags Priority, FBulkDataIORequestCallBack* CompleteCallback, uint8* UserSuppliedMemory) const
{
	const int64 DataSize = GetBulkDataSize();

	return CreateStreamingRequest(0, DataSize, Priority, CompleteCallback, UserSuppliedMemory);
}

IBulkDataIORequest* FBulkDataBase::CreateStreamingRequest(int64 OffsetInBulkData, int64 BytesToRead, EAsyncIOPriorityAndFlags Priority, FBulkDataIORequestCallBack* CompleteCallback, uint8* UserSuppliedMemory) const
{
	if (IsUsingIODispatcher())
	{
		FBulkDataIoDispatcherRequest* IoRequest = new FBulkDataIoDispatcherRequest(ChunkID, OffsetInBulkData, BytesToRead, CompleteCallback, UserSuppliedMemory);	
		IoRequest->StartAsyncWork();

		return IoRequest;
	}
	else
	{
		const int64 BulkDataSize = GetBulkDataSize();
		FileTokenSystem::Data FileData = FileTokenSystem::GetFileData(Fallback.Token);

		check(FileData.PackageHeaderFilename.IsEmpty() == false);
		const FString Filename = ConvertFilenameFromFlags(FileData.PackageHeaderFilename);

		UE_CLOG(IsStoredCompressedOnDisk(), LogSerialization, Fatal, TEXT("Package level compression is no longer supported (%s)."), *Filename);
		UE_CLOG(BulkDataSize <= 0, LogSerialization, Error, TEXT("(%s) has invalid bulk data size."), *Filename);

		IAsyncReadFileHandle* IORequestHandle = FPlatformFileManager::Get().GetPlatformFile().OpenAsyncRead(*Filename);
		check(IORequestHandle); // this generally cannot fail because it is async

		if (IORequestHandle == nullptr)
		{
			return nullptr;
		}

		const int64 OffsetInFile = FileData.BulkDataOffsetInFile + OffsetInBulkData;

		FBulkDataIORequest* IORequest = new FBulkDataIORequest(IORequestHandle);

		if (IORequest->MakeReadRequest(OffsetInFile, BytesToRead, Priority, CompleteCallback, UserSuppliedMemory))
		{
			return IORequest;
		}
		else
		{
			delete IORequest;
			return nullptr;
		}
	}
}

IBulkDataIORequest* FBulkDataBase::CreateStreamingRequestForRange(const BulkDataRangeArray& RangeArray, EAsyncIOPriorityAndFlags Priority, FBulkDataIORequestCallBack* CompleteCallback)
{
	check(RangeArray.Num() > 0);

	const FBulkDataBase& Start = *(RangeArray[0]);

	check(!Start.IsInlined());

	if (Start.IsUsingIODispatcher())
	{
		FBulkDataIoDispatcherRequest::ChunkIdArray ChunkIds;
		for (const FBulkDataBase* BulkData : RangeArray)
		{
			ChunkIds.Push(BulkData->ChunkID);
		}

		FBulkDataIoDispatcherRequest* IoRequest = new FBulkDataIoDispatcherRequest(ChunkIds, CompleteCallback);
		IoRequest->StartAsyncWork();

		return IoRequest;
	}
	else
	{	
		const FBulkDataBase& End = *RangeArray[RangeArray.Num() - 1];

		check(Start.GetFilename() == End.GetFilename());

		const int64 ReadOffset = Start.GetBulkDataOffsetInFile();
		const int64 ReadSize = (End.GetBulkDataOffsetInFile() + End.GetBulkDataSize()) - ReadOffset;

		check(ReadSize > 0);

		return Start.CreateStreamingRequest(0, ReadSize, Priority, CompleteCallback, nullptr);
	}
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

	if (!IsUsingIODispatcher())
	{ 
		FileTokenSystem::UnregisterFileToken(Fallback.Token);
		Fallback.Token = InvalidToken;
	}

	BulkDataFlags = 0;

}

int64 FBulkDataBase::GetBulkDataOffsetInFile() const
{
	if (!IsUsingIODispatcher())
	{
		return FileTokenSystem::GetBulkDataOffset(Fallback.Token);
	}
	else
	{
		// When using the IODispatcher the BulkData object will point directly to the correct data
		// so we don't need to consider the offset at all.
		return 0;
	}
}

FString FBulkDataBase::GetFilename() const
{
	if (!IsUsingIODispatcher())
	{
		FString Filename = FileTokenSystem::GetFilename(Fallback.Token);
		return ConvertFilenameFromFlags(Filename);
	}
	else
	{
		UE_LOG(LogBulkDataRuntime, Warning, TEXT("Attempting to get the filename for BulkData that uses the IoDispatcher, this will return an empty string"));
		return FString("");
	}
}

bool FBulkDataBase::CanDiscardInternalData() const
{
	// We can discard the data if:
	// -	We can reload the Bulkdata from disk
	// -	If the Bulkdata object has been marked as single use which shows 
	//		that there is no intent to access the data again)
	// -	If we are using the IoDispatcher and the data is currently inlined
	//		since we will not be able to reload inline data when the IoStore is
	//		active.

	// TODO: This is currently called from ::GetCopy but not ::Unlock, we should investe unifying the
	// rules for discarding data
	return CanLoadFromDisk() || IsSingleUse() || (IsInlined() && IsIoDispatcherEnabled());
}

void FBulkDataBase::LoadDataDirectly(void** DstBuffer)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FBulkDataBase::LoadDataDirectly"), STAT_UBD_LoadDataDirectly, STATGROUP_Memory);
	
	if (!CanLoadFromDisk())
	{
		UE_LOG(LogSerialization, Warning, TEXT("Attempting to load a BulkData object that cannot be loaded from disk"));
		return; // Early out if there is nothing to load anyway
	}

	if (!IsIoDispatcherEnabled())
	{
		const int64 BulkDataSize = GetBulkDataSize();
		FileTokenSystem::Data FileData = FileTokenSystem::GetFileData(Fallback.Token);

		FString Filename; 
		int64 Offset = FileData.BulkDataOffsetInFile;

		// Fix up the Filename/Offset to work with streaming if EDL is enabled and the filename is still referencing a uasset or umap
		if (IsInlined() && (FileData.PackageHeaderFilename.EndsWith(TEXT(".uasset")) || FileData.PackageHeaderFilename.EndsWith(TEXT(".umap"))))
		{
			Offset -= IFileManager::Get().FileSize(*FileData.PackageHeaderFilename);
			Filename = FPaths::GetBaseFilename(FileData.PackageHeaderFilename, false) + TEXT(".uexp");
		}
		else
		{
			Filename = ConvertFilenameFromFlags(FileData.PackageHeaderFilename);
		}

		// If the data is inlined then we already loaded is during ::Serialize, this warning should help track cases where data is being discarded then re-requested.
		// Disabled at the moment
		UE_CLOG(IsInlined(), LogSerialization, Warning, TEXT("Reloading inlined bulk data directly from disk, this is detrimental to loading performance. Filename: '%s'."), *Filename);

		FArchive* Ar = IFileManager::Get().CreateFileReader(*Filename, FILEREAD_Silent);
		checkf(Ar != nullptr, TEXT("Failed to open the file to load bulk data from. Filename: '%s'."), *Filename);

		// Seek to the beginning of the bulk data in the file.
		Ar->Seek(Offset);

		if (*DstBuffer == nullptr)
		{
			*DstBuffer = FMemory::Malloc(BulkDataSize, 0);
		}

		SerializeBulkData(*Ar, *DstBuffer, BulkDataSize);

		delete Ar;
	}
	else if (IsUsingIODispatcher())
	{
		// Allocate the buffer if needed
		if (*DstBuffer == nullptr)
		{
			*DstBuffer = FMemory::Malloc(GetBulkDataSize(), 0);
		}

		// Set up our options (we only need to set the target)
		FIoReadOptions Options;
		Options.SetTargetVa(*DstBuffer);
		
		FIoBatch NewBatch = GetIoDispatcher()->NewBatch();
		FIoRequest Request = NewBatch.Read(ChunkID, Options);

		NewBatch.Issue();
		NewBatch.Wait(); // Blocking wait until all requests in the batch are done

		check(Request.IsOk());

		FBulkDataBase::GetIoDispatcher()->FreeBatch(NewBatch);
	}
	else
	{
		// Note that currently this shouldn't be reachable as we should early out due to the ::CanLoadFromDisk check at the start of the method
		UE_LOG(LogSerialization, Error, TEXT("Attempting to reload inline BulkData when the IoDispatcher is enabled, this operation is not supported! (%d)"), IsInlined());
	}
}

void FBulkDataBase::ProcessDuplicateData(FArchive& Ar, const UPackage* Package, const FString* Filename, int64& InOutSizeOnDisk, int64& InOutOffsetInFile)
{
	// We need to load the optional bulkdata info as we might need to create a FIoChunkId based on it!
	uint32 NewFlags;
	int64 NewSizeOnDisk;
	int64 NewOffset;

	SerializeDuplicateData(Ar, NewFlags, NewSizeOnDisk, NewOffset);

#if ALLOW_OPTIONAL_DATA
	if (IsUsingIODispatcher())
	{
		const int64 BulkDataID = NewSizeOnDisk > 0 ? NewOffset : TNumericLimits<uint64>::Max();
		FIoChunkId OptionalChunkId = CreateBulkdataChunkId(Package->GetPackageId().ToIndex(), BulkDataID, EIoChunkType::OptionalBulkData);

		if (IoDispatcher->DoesChunkExist(OptionalChunkId))
		{
			BulkDataFlags = NewFlags | BULKDATA_UsesIoDispatcher;
			InOutSizeOnDisk = NewSizeOnDisk;
			InOutOffsetInFile = NewOffset;

			ChunkID = OptionalChunkId;
		}
	}
	else
	{
		check(Filename != nullptr);
		const FString OptionalDataFilename = ConvertFilenameFromFlags(*Filename);

		if (IFileManager::Get().FileExists(*OptionalDataFilename))
		{
			BulkDataFlags = NewFlags;
			InOutSizeOnDisk = NewSizeOnDisk;
			InOutOffsetInFile = NewOffset;

			// Note we do not override Filename with OptionalDataFilename as we are supposed to store the original!
			Fallback.Token = InvalidToken;
			Fallback.BulkDataSize = InOutSizeOnDisk;
		}
	}
#endif
}

void FBulkDataBase::SerializeDuplicateData(FArchive& Ar, uint32& OutBulkDataFlags, int64& OutBulkDataSizeOnDisk, int64& OutBulkDataOffsetInFile)
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

	if ((OutBulkDataFlags & BULKDATA_UsesIoDispatcher) != 0)
	{
		uint16 DummyBulkDataIndex = InvalidBulkDataIndex;
		Ar << DummyBulkDataIndex;
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

FString FBulkDataBase::ConvertFilenameFromFlags(const FString& Filename) const
{	
	if (IsOptional())
	{
		// Optional data should be tested for first as we in theory can have data that would
		// be marked as inline, also marked as optional and in this case we should treat it as
		// optional data first.
		return FPaths::ChangeExtension(Filename, OptionalExt);
	}
	else if (!IsInSeperateFile())
	{
		return Filename;
	}
	else if (IsInlined())
	{
		return FPaths::ChangeExtension(Filename, InlinedExt);
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
