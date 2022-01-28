// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/BulkData2.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "Misc/PackageSegment.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/LoadTimeTracker.h"
#include "Serialization/BulkData.h"
#include "Serialization/LargeMemoryReader.h"
#include "UObject/LinkerLoad.h"
#include "UObject/Object.h"
#include "UObject/Package.h"
#include "UObject/PackageResourceManager.h"
#include "IO/IoDispatcher.h"
#include "Async/Async.h"
#include "Async/MappedFileHandle.h"
#include "AsyncLoadingPrivate.h"

DEFINE_LOG_CATEGORY_STATIC(LogBulkDataRuntime, Log, All);

IMPLEMENT_TYPE_LAYOUT(FBulkDataBase);

// If set to 0 then we will pretend that optional data does not exist, useful for testing.
#define ALLOW_OPTIONAL_DATA 1

// When set to 1 attempting to reload inline data will fail with the old loader in the same way that it fails in
// the new loader to keep the results consistent.
#define UE_KEEP_INLINE_RELOADING_CONSISTENT 0

// Used to validate FIoStatus return values and assert if there is an error
#define CHECK_IOSTATUS( InIoStatus, InMethodName ) checkf(InIoStatus.IsOk(), TEXT("%s failed: %s"), InMethodName, *InIoStatus.ToString());

namespace
{
	const uint16 InvalidBulkDataIndex = ~uint16(0);

	/** 
	 * Returns true if we should not trigger an ensure if we detect an inline bulkdata reload request that will not work with the 
	 * IoStore system. This can be done by setting [Core.System]IgnoreInlineBulkDataReloadEnsures to true in the config file.
	 * It is NOT recommended that you do this, but is provded in case of unforseen use cases.
	 */
	bool ShouldIgnoreInlineDataReloadEnsures()
	{
		static struct FIgnoreInlineDataReloadEnsures
		{
			bool bEnabled = false;

			FIgnoreInlineDataReloadEnsures()
			{
				FConfigFile PlatformEngineIni;
				FConfigCacheIni::LoadLocalIniFile(PlatformEngineIni, TEXT("Engine"), true, ANSI_TO_TCHAR(FPlatformProperties::IniPlatformName()));

				PlatformEngineIni.GetBool(TEXT("Core.System"), TEXT("IgnoreInlineBulkDataReloadEnsures"), bEnabled);

				UE_LOG(LogSerialization, Display, TEXT("IgnoreInlineDataReloadEnsures: '%s'"), bEnabled ? TEXT("true") : TEXT("false"));
			}
		} IgnoreInlineDataReloadEnsures;

		return IgnoreInlineDataReloadEnsures.bEnabled;
	}

	const FIoFilenameHash FALLBACK_IO_FILENAME_HASH = INVALID_IO_FILENAME_HASH - 1;
}

FIoFilenameHash MakeIoFilenameHash(const FPackagePath& PackagePath)
{
	if (!PackagePath.IsEmpty())
	{
		FString BaseFileName = PackagePath.GetLocalBaseFilenameWithPath().ToLower();
		const FIoFilenameHash Hash = FCrc::StrCrc32<TCHAR>(*BaseFileName);
		return Hash != INVALID_IO_FILENAME_HASH ? Hash : FALLBACK_IO_FILENAME_HASH;
	}
	else
	{
		return INVALID_IO_FILENAME_HASH;
	}
}

FIoFilenameHash MakeIoFilenameHash(const FString& Filename)
{
	if (!Filename.IsEmpty())
	{
		FString BaseFileName = FPaths::GetBaseFilename(Filename).ToLower();
		const FIoFilenameHash Hash = FCrc::StrCrc32<TCHAR>(*BaseFileName);
		return Hash != INVALID_IO_FILENAME_HASH ? Hash : FALLBACK_IO_FILENAME_HASH;
	}
	else
	{
		return INVALID_IO_FILENAME_HASH;
	}
}

FIoFilenameHash MakeIoFilenameHash(const FIoChunkId& ChunkID)
{
	if (ChunkID.IsValid())
	{
		const FIoFilenameHash Hash = GetTypeHash(ChunkID);
		return Hash != INVALID_IO_FILENAME_HASH ? Hash : FALLBACK_IO_FILENAME_HASH;
	}
	else
	{
		return INVALID_IO_FILENAME_HASH;
	}
}

namespace PackageTokenSystem
{
	// Internal to the PackageTokenSystem namespace
	namespace
	{
		struct FPayloadData
		{
			FPackagePath PackagePath;
			uint16 RefCount;
		};

		/**
		* Provides a ref counted PackageName->FPackagePath look up table.
		*/
		class FPackageDataTable
		{
		public:
			using KeyType = uint64;

			void Add(const KeyType& Key, const FPackagePath& PackagePath)
			{
				if (FPayloadData* ExistingEntry = Table.Find(Key))
				{
					ExistingEntry->RefCount++;
					checkf(ExistingEntry->PackagePath == PackagePath, TEXT("PackagePath mismatch!"));
				}
				else
				{
					FPayloadData& NewEntry = Table.Emplace(Key);
					NewEntry.PackagePath = PackagePath;
					NewEntry.RefCount = 1;
				}
			}

			bool Remove(const KeyType& Key)
			{
				if (FPayloadData* ExistingEntry = Table.Find(Key))
				{
					if (--ExistingEntry->RefCount == 0)
					{
						Table.Remove(Key);
						return true;
					}
				}

				return false;
			}

			void IncRef(const KeyType& Key)
			{
				if (FPayloadData* ExistingEntry = Table.Find(Key))
				{
					ExistingEntry->RefCount++;
				}
			}

			const FPackagePath& Resolve(const KeyType& Key)
			{
				return Table.Find(Key)->PackagePath;
			}

			int32 Num() const
			{
				return Table.Num();
			}

		private:
			TMap<KeyType, FPayloadData> Table;
		};
	}

	FPackageDataTable PackageDataTable;

	FRWLock TokenLock;

	FBulkDataOrId::FileToken RegisterToken( const FName& PackageName, const FPackagePath& PackagePath)
	{
		const uint64 Token = (uint64(PackageName.GetComparisonIndex().ToUnstableInt()) << 32) | uint64(PackageName.GetNumber());

		FWriteScopeLock LockForScope(TokenLock);
		PackageDataTable.Add(Token, PackagePath);

		return Token;
	}

	void UnRegisterToken(FBulkDataOrId::FileToken ID)
	{
		if (ID != FBulkDataBase::InvalidToken)
		{
			FWriteScopeLock LockForScope(TokenLock);

			PackageDataTable.Remove(ID);
		}
	}

	FBulkDataOrId::FileToken CopyToken(FBulkDataOrId::FileToken ID)
	{
		if (ID != FBulkDataBase::InvalidToken)
		{
			FWriteScopeLock LockForScope(TokenLock);

			PackageDataTable.IncRef(ID);

			return ID;
		}
		else
		{
			return FBulkDataBase::InvalidToken;
		}
	}

	FPackagePath GetPackagePath(FBulkDataOrId::FileToken ID)
	{
		if (ID == FBulkDataBase::InvalidToken)
		{
			return FPackagePath();
		}

		FReadScopeLock LockForScope(TokenLock);
		return PackageDataTable.Resolve(ID);
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
		// Even though SetComplete called in the constructor and sets bCompleteAndCallbackCalled=true, we still need to implement WaitComplete as
		// the CompleteCallback can end up starting async tasks that can overtake the constructor execution and need to wait for the constructor to finish.
		while (!*(volatile bool*)&bCompleteAndCallbackCalled);
	}

	virtual void CancelImpl() override
	{
		// No point canceling as the work is done in the constructor
	}
};

// Currently we share the single critical section between all FReadChunkIdRequest to match how the older 
// implementation for reading from pak files was managing multi-threaded access.
// TODO: Profile this to see if it is worth replacing with a single critical section per FReadChunkIdRequest.
static FCriticalSection FReadChunkIdRequestEvent;

class FReadChunkIdRequest : public IAsyncReadRequest
{
public:
	FReadChunkIdRequest(const FIoChunkId& InChunkId, FAsyncFileCallBack* InCallback, uint8* InUserSuppliedMemory, int64 InOffset, int64 InBytesToRead, int32 InPriority)
		: IAsyncReadRequest(InCallback, false, InUserSuppliedMemory)
	{
		// Because IAsyncReadRequest can return ownership of the target memory buffer in the form
		// of a raw pointer we must pass our own memory buffer to the FIoDispatcher otherwise the 
		// buffer that will be returned cannot have it's lifetime managed correctly.
		if (InUserSuppliedMemory == nullptr)
		{
			Memory = (uint8*)FMemory::Malloc(InBytesToRead);
		}

		FIoReadOptions Options(InOffset, InBytesToRead);
		Options.SetTargetVa(Memory);

		auto OnRequestLoaded = [this](TIoStatusOr<FIoBuffer> Result)
		{
			if (!Result.Status().IsOk())
			{
				//TODO: Enable logging when we can give some more useful message
				//UE_LOG(LogSerialization, Error, TEXT("FReadChunkIdRequest failed: %s"), *Result.Status().ToString());
				// If there was an IO error then we need to count the request as canceled
				bCanceled = true;
			}

			SetDataComplete();

			{
				FScopeLock Lock(&FReadChunkIdRequestEvent);
				bRequestOutstanding = false;

				if (DoneEvent != nullptr)
				{
					DoneEvent->Trigger();
				}

				SetAllComplete();
			}
		};

		FIoBatch IoBatch = FBulkDataBase::GetIoDispatcher()->NewBatch();
		IoRequest = IoBatch.ReadWithCallback(InChunkId, Options, InPriority, OnRequestLoaded);
		IoBatch.Issue();
	}

	virtual ~FReadChunkIdRequest()
	{
		// Make sure no other thread is waiting on this request
		checkf(DoneEvent == nullptr, TEXT("A thread is still waiting on a FReadChunkIdRequest that is being destroyed!")); 

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
		// Make sure no other thread is waiting on this request
		checkf(DoneEvent == nullptr, TEXT("Multiple threads attempting to wait on the same FReadChunkIdRequest"));

		{
			FScopeLock Lock(&FReadChunkIdRequestEvent);
			if (bRequestOutstanding)
			{
				checkf(DoneEvent == nullptr, TEXT("Multiple threads attempting to wait on the same FReadChunkIdRequest"));
				DoneEvent = FPlatformProcess::GetSynchEventFromPool(true);
			}
		}

		if (DoneEvent != nullptr)
		{
			uint32 TimeLimitMilliseconds = TimeLimitSeconds <= 0.0f ? MAX_uint32 : (uint32)(TimeLimitSeconds * 1000.0f);
			DoneEvent->Wait(TimeLimitMilliseconds);

			FScopeLock Lock(&FReadChunkIdRequestEvent);
			FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
			DoneEvent = nullptr;
		}

		// Make sure everything has completed
		checkf(bRequestOutstanding == false, TEXT("Request has not completed by the end of WaitCompletionImpl"));
		checkf(PollCompletion() == true, TEXT("Request and callback has not completed by the end of WaitCompletionImpl"));
	}

	virtual void CancelImpl() override
	{
		bCanceled = true;

		IoRequest.Cancel();
	}

	/** The ChunkId that is being read. */
	FIoChunkId ChunkId;
	/** Pending io request */
	FIoRequest IoRequest;
	/** Only actually gets created if WaitCompletion is called. */
	FEvent* DoneEvent = nullptr;
	/** True while the request is pending, true once it has either been completed or canceled. */
	bool bRequestOutstanding = true;
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
		return new FReadChunkIdRequest(ChunkID, CompleteCallback, UserSuppliedMemory, Offset, BytesToRead, ConvertToIoDispatcherPriority(PriorityAndFlags));
	}
private:
	FIoChunkId ChunkID;
};

namespace UE::BulkData::Private
{

IAsyncReadFileHandle* CreateAsyncReadHandle(const FIoChunkId& InChunkID)
{
	return new FAsyncReadChunkIdHandle(InChunkID);
}

} // namespace UE::BulkData::Private

static FCriticalSection FBulkDataIoDispatcherRequestEvent;

class FBulkDataIoDispatcherRequest final : public IBulkDataIORequest
{
public:
	FBulkDataIoDispatcherRequest(const FIoChunkId& InChunkID, int64 InOffsetInBulkData, int64 InBytesToRead, int32 Priority, FBulkDataIORequestCallBack* InCompleteCallback, uint8* InUserSuppliedMemory)
		: UserSuppliedMemory(InUserSuppliedMemory)
	{
		RequestArray.Push({ InChunkID, (uint64)InOffsetInBulkData , (uint64)InBytesToRead, Priority });

		if (InCompleteCallback != nullptr)
		{
			CompleteCallback = *InCompleteCallback;
		}
	}

	FBulkDataIoDispatcherRequest(const FIoChunkId& InChunkID, int32 Priority, FBulkDataIORequestCallBack* InCompleteCallback)
		: UserSuppliedMemory(nullptr)
	{
		const uint64 Size = FBulkDataBase::GetIoDispatcher()->GetSizeForChunk(InChunkID).ConsumeValueOrDie();
		RequestArray.Push({ InChunkID, 0, Size, Priority });

		if (InCompleteCallback != nullptr)
		{
			CompleteCallback = *InCompleteCallback;
		}
	}

	virtual ~FBulkDataIoDispatcherRequest()
	{
		FBulkDataIoDispatcherRequest::WaitCompletion(0.0f); // Wait for ever as we cannot leave outstanding requests

		// Free the data is no caller has taken ownership of it and it was allocated by FBulkDataIoDispatcherRequest
		if (UserSuppliedMemory == nullptr)
		{
			FMemory::Free(DataResult);
			DataResult = nullptr;
		}

		// Make sure no other thread is waiting on this request
		checkf(DoneEvent == nullptr, TEXT("A thread is still waiting on a FBulkDataIoDispatcherRequest that is being destroyed!"));
	}

	void StartAsyncWork()
	{		
		checkf(RequestArray.Num() > 0, TEXT("RequestArray cannot be empty"));

		auto Callback = [this]()
		{
			bool bIsOk = true;
			for (Request& Request : RequestArray)
			{
				bIsOk &= Request.IoRequest.Status().IsOk();
			}
			if (bIsOk)
			{
				SizeResult = IoBuffer.DataSize();

				if (IoBuffer.IsMemoryOwned())
				{
					DataResult = IoBuffer.Release().ConsumeValueOrDie();
				}
				else
				{
					DataResult = IoBuffer.Data();
				}
			}
			else
			{
				//TODO: Enable logging when we can give some more useful message
				//UE_LOG(LogSerialization, Error, TEXT("FBulkDataIoDispatcherRequest failed: %s"), *Result.Status().ToString());
				// If there was an IO error then we need to count the request as canceled
				bIsCanceled = true;
			}

			bDataIsReady = true;

			if (CompleteCallback)
			{
				CompleteCallback(bIsCanceled, this);
			}

			{
				FScopeLock Lock(&FReadChunkIdRequestEvent);
				bIsCompleted = true;

				if (DoneEvent != nullptr)
				{
					DoneEvent->Trigger();
				}
			}
		};

		FIoBatch IoBatch = FBulkDataBase::GetIoDispatcher()->NewBatch();

		uint64 TotalSize = 0;
		for (const Request& Request : RequestArray)
		{
			//checkf(!Request.IoRequest.IsValid(), TEXT("FBulkDataIoDispatcherRequest::StartAsyncWork was called twice"));
			TotalSize += Request.BytesToRead;
		}
		if (UserSuppliedMemory != nullptr)
		{
			IoBuffer = FIoBuffer(FIoBuffer::Wrap, UserSuppliedMemory, TotalSize);
		}
		else
		{
			IoBuffer = FIoBuffer(FIoBuffer::AssumeOwnership, FMemory::Malloc(TotalSize), TotalSize);
		}
		uint8* Dst = IoBuffer.Data();
		for (Request& Request : RequestArray)
		{
			FIoReadOptions ReadOptions(Request.OffsetInBulkData, Request.BytesToRead);
			ReadOptions.SetTargetVa(Dst);
			Request.IoRequest = IoBatch.Read(Request.ChunkId, ReadOptions, Request.Priority);
			Dst += Request.BytesToRead;
		}

		IoBatch.IssueWithCallback(Callback);
	}

	virtual bool PollCompletion() const override
	{
		return bIsCompleted;
	}

	virtual bool WaitCompletion(float TimeLimitSeconds) override
	{
		// Make sure no other thread is waiting on this request
		checkf(DoneEvent == nullptr, TEXT("Multiple threads attempting to wait on the same FBulkDataIoDispatcherRequest"));

		{
			FScopeLock Lock(&FReadChunkIdRequestEvent);
			if (!bIsCompleted)
			{
				checkf(DoneEvent == nullptr, TEXT("Multiple threads attempting to wait on the same FBulkDataIoDispatcherRequest"));
				DoneEvent = FPlatformProcess::GetSynchEventFromPool(true);
			}
		}

		if (DoneEvent != nullptr)
		{
			uint32 TimeLimitMilliseconds = TimeLimitSeconds <= 0.0f ? MAX_uint32 : (uint32)(TimeLimitSeconds * 1000.0f);
			DoneEvent->Wait(TimeLimitMilliseconds);

			FScopeLock Lock(&FReadChunkIdRequestEvent);
			FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
			DoneEvent = nullptr;
		}

		return bIsCompleted;
	}

	virtual uint8* GetReadResults() override
	{
		if (bDataIsReady && !bIsCanceled)
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
		if (bDataIsReady && !bIsCanceled)
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
			for (Request& Request : RequestArray)
			{
				Request.IoRequest.Cancel();
			}
		}
	}

private:
	struct Request
	{
		FIoChunkId ChunkId;
		uint64 OffsetInBulkData;
		uint64 BytesToRead;
		int32 Priority;
		FIoRequest IoRequest;
	};

	TArray<Request, TInlineAllocator<8>> RequestArray;

	FBulkDataIORequestCallBack CompleteCallback;
	uint8* UserSuppliedMemory = nullptr;

	uint8* DataResult = nullptr;
	int64 SizeResult = 0;

	bool bDataIsReady = false;

	bool bIsCompleted = false;
	bool bIsCanceled = false;

	/** Only actually gets created if WaitCompletion is called. */
	FEvent* DoneEvent = nullptr;

	FIoBuffer IoBuffer;
};

TUniquePtr<IBulkDataIORequest> CreateBulkDataIoDispatcherRequest(
	const FIoChunkId& InChunkID,
	int64 InOffsetInBulkData,
	int64 InBytesToRead,
	FBulkDataIORequestCallBack* InCompleteCallback,
	uint8* InUserSuppliedMemory,
	int32 InPriority)
{
	TUniquePtr<FBulkDataIoDispatcherRequest> Request;

	if (InBytesToRead > 0)
	{
		Request.Reset(new FBulkDataIoDispatcherRequest(InChunkID, InOffsetInBulkData, InBytesToRead, InPriority, InCompleteCallback, InUserSuppliedMemory));
	}
	else
	{
		// Add some asserts to make sure that the caller won't be surprised when their parameters are ignored.
		checkf(InOffsetInBulkData > 0, TEXT("InOffsetInBulkData would be ignored"));
		checkf(InUserSuppliedMemory == nullptr, TEXT("InUserSuppliedMemory would be ignored"));
		Request.Reset(new FBulkDataIoDispatcherRequest(InChunkID, InPriority, InCompleteCallback));
	}

	Request->StartAsyncWork();

	return Request;
}

FBulkDataBase::FBulkDataBase(FBulkDataBase&& Other)
	: Data(Other.Data) // Copies the entire union
	, DataAllocation(Other.DataAllocation)
	, BulkDataSize(Other.BulkDataSize)
	, BulkDataOffset(Other.BulkDataOffset)
	, BulkDataFlags(Other.BulkDataFlags)

{
	checkf(Other.LockStatus != LOCKSTATUS_ReadWriteLock, TEXT("Attempting to read from a BulkData object that is locked for write")); 

	if (!Other.IsUsingIODispatcher())
	{
		Other.Data.Token = InvalidToken; // Prevent the other object from unregistering the token
	}	
}

FBulkDataBase& FBulkDataBase::operator=(const FBulkDataBase& Other)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FBulkDataBase::operator="), STAT_UBD_Constructor, STATGROUP_Memory);

	checkf(LockStatus == LOCKSTATUS_Unlocked, TEXT("Attempting to modify a BulkData object that is locked"));
	checkf(Other.LockStatus != LOCKSTATUS_ReadWriteLock, TEXT("Attempting to read from a BulkData object that is locked for write"));

	RemoveBulkData();

	if (Other.IsUsingIODispatcher())
	{
		Data.PackageID = Other.Data.PackageID;
	}
	else
	{
		Data.Token = PackageTokenSystem::CopyToken(Other.Data.Token);
	}

	BulkDataSize = Other.BulkDataSize;
	BulkDataOffset = Other.BulkDataOffset;
	BulkDataFlags = Other.BulkDataFlags;

	if (!Other.IsDataMemoryMapped() || !Other.IsInSeparateFile())
	{
		if (Other.GetDataBufferReadOnly())
		{
			void* Dst = AllocateData(BulkDataSize);
			FMemory::Memcpy(Dst, Other.GetDataBufferReadOnly(), BulkDataSize);
		}
	}
	else
	{
		// Note we don't need a fallback since the original already managed the load, if we fail now then it
		// is an actual error.
		if (Other.IsUsingIODispatcher())
		{
			TIoStatusOr<FIoMappedRegion> Status = IoDispatcher->OpenMapped(CreateChunkId(), FIoReadOptions());
			FIoMappedRegion MappedRegion = Status.ConsumeValueOrDie();
			DataAllocation.SetMemoryMappedData(this, MappedRegion.MappedFileHandle, MappedRegion.MappedFileRegion);
		}
		else
		{
			FPackagePath PackagePath = PackageTokenSystem::GetPackagePath(Data.Token);
			MemoryMapBulkData(PackagePath, GetPackageSegmentFromFlags(), BulkDataOffset, BulkDataSize);
		}
	}

	return *this;
}

FBulkDataBase::~FBulkDataBase()
{
	FlushAsyncLoading();

	checkf(LockStatus == LOCKSTATUS_Unlocked, TEXT("Attempting to modify a BulkData object that is locked"));

	FreeData();
	if (!IsUsingIODispatcher())
	{
		PackageTokenSystem::UnRegisterToken(Data.Token);
	}
}

void FBulkDataBase::ConditionalSetInlineAlwaysAllowDiscard(bool bPackageUsesIoStore)
{
	// If PackagePath is null and we do not have BULKDATA_UsesIoDispatcher, we will not be able to reload the bulkdata.
	// We will not have a PackagePath if the engine is using IoStore.
	// So in IoStore with BulkData stored inline, we can not reload the bulkdata.
	// We do not need to consider the end-of-file bulkdata section because IoStore  guarantees during cooking
	// (SaveBulkData) that only inlined data is in the package file; there is no end-of-file bulkdata section.

	// In the Inlined IoStore case therefore we need to not discard the bulk data if !BULKDATA_SingleUse;
	// we have to keep it in case it gets requested twice.
	// However, some systems (Audio,Animation) have large inline data they for legacy reasons have not marked as
	// BULKDATA_SingleUse.
	// In the old loader this data was discarded for them since CanLoadFromDisk was true.
	// Since CanLoadFromDisk is now false, we are keeping that data around, and this causes memory bloat.
	// Licensees have asked us to fix this memory bloat.
	//
	// To hack-fix the memory bloat in these systems when using IoStore we will mark all inline BulkData
	// as discardable when using IoStore.
	// This will cause a bug when using IoStore in any systems that do actually need to reload inline data.
	// Each project that uses IoStore needs to guarantee that they do not have any systems that need to
	// reload inline data.
	// Note that when the define UE_KEEP_INLINE_RELOADING_CONSISTENT is enabled the old loading path should also
	// not allow the reloading of inline data so that it behaves the same way as the new loading path. So when it
	// is enabled we do not need to check if the loader is or isn't enabled, we can just set the flag and assume
	// all inline data should be allowed to be discarded.

#if !UE_KEEP_INLINE_RELOADING_CONSISTENT
	if (bPackageUsesIoStore)
#endif // !UE_KEEP_INLINE_RELOADING_CONSISTENT
	{
		SetBulkDataFlags(BULKDATA_AlwaysAllowDiscard);
	}
}

void FBulkDataBase::Serialize(FArchive& Ar, UObject* Owner, int32 /*Index*/, bool bAttemptFileMapping, int32 ElementSize)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FBulkDataBase::Serialize"), STAT_UBD_Serialize, STATGROUP_Memory);

	SCOPED_LOADTIMER(BulkData_Serialize);

#if WITH_EDITOR == 0 && WITH_EDITORONLY_DATA == 0
	if (Ar.IsPersistent() && !Ar.IsObjectReferenceCollector() && !Ar.ShouldSkipBulkData())
	{		
		checkf(Ar.IsLoading(), TEXT("FBulkDataBase only works with loading")); // Only support loading from cooked data!
		checkf(!GIsEditor, TEXT("FBulkDataBase does not work in the editor"));
		checkf(LockStatus == LOCKSTATUS_Unlocked, TEXT("Attempting to modify a BulkData object that is locked"));

		Ar << BulkDataFlags;

		int64 ElementCount = 0;
		int64 BulkDataSizeOnDisk = 0;

		BulkDataOffset = 0;

		SerializeBulkDataSizeInt(Ar, ElementCount, BulkDataFlags);
		SerializeBulkDataSizeInt(Ar, BulkDataSizeOnDisk, BulkDataFlags);

		BulkDataSize = ElementCount * ElementSize;

		Ar << BulkDataOffset;

		if ((BulkDataFlags & BULKDATA_BadDataVersion) != 0)
		{
			uint16 DummyValue;
			Ar << DummyValue;
		}

		EBulkDataFlags DuplicateFlags = static_cast<EBulkDataFlags>(0);
		int64 DuplicateSizeOnDisk = INDEX_NONE;
		int64 DuplicateOffset = INDEX_NONE;
		if (IsDuplicateNonOptional())
		{
			SerializeDuplicateData(Ar, DuplicateFlags, DuplicateSizeOnDisk, DuplicateOffset);
		}
		checkf(!(BulkDataFlags & BULKDATA_WorkspaceDomainPayload), TEXT("FBulkDataBase error on %s:")
			TEXT("FBulkDataBase does not support BULKDATA_WorkspaceDomainPayload"), *Ar.GetArchiveName());

		// We assume that Owner/Package/Linker are all valid. The old BulkData system would
		// generally fail if any of these were nullptr but had plenty of inconsistent checks
		// scattered throughout.
		checkf(Owner != nullptr, TEXT("FBulkDataBase::Serialize requires a valid Owner"));
		const UPackage* Package = Owner->GetOutermost();
		checkf(Package != nullptr, TEXT("FBulkDataBase::Serialize requires an Owner that returns a valid UPackage"));

		const bool bPackageUsesIoStore = IsPackageLoadingFromIoDispatcher(Package, Ar);
		const FPackagePath* PackagePath = nullptr;
		const FLinkerLoad* Linker = nullptr;
		if (bPackageUsesIoStore)
		{
			checkf(IsInlined() || !NeedsOffsetFixup(), TEXT("IODispatcher does not support offset fixups; SaveBulkData during cooking should have added the flag BULKDATA_NoOffsetFixUp."));
			checkf(IsInlined() || IsInSeparateFile() || !GEventDrivenLoaderEnabled,
				TEXT("IODispatcher does not support finding the file size of header segments, which is required if BulkData is at end-of-file and EDL is enabled. ")
				TEXT("Non-inline BulkData must be stored in a separate file when EDL is enabled!"));
			if (IsInSeparateFile())
			{
				Data.PackageID = Package->GetPackageIdToLoad().Value();
				SetRuntimeBulkDataFlags(BULKDATA_UsesIoDispatcher); // Indicates that this BulkData should use the FIoChunkId rather than a PackagePath
			}
			else
			{
				// this->Data will be unused since we are loading from the current file; initialize it to invalid
				Data.Token = InvalidToken;
			}
		}
		else
		{
			Linker = FLinkerLoad::FindExistingLinkerForPackage(Package);
			if (Linker != nullptr)
			{
				PackagePath = &Linker->GetPackagePath();
			}
			if (!PackagePath || PackagePath->IsEmpty())
			{
				// Note that this Bulkdata object will end up with an invalid token and will end up resolving to an empty file path!
				UE_LOG(LogSerialization, Warning, TEXT("Could not get PackagePath from linker for package %s!"), *Package->GetName());
				PackagePath = nullptr;
			}

			if (!IsInlined() && NeedsOffsetFixup())
			{
				// The offset can be set relative to the start of the BulkData section during cooking. Convert it back to an offset from the start of the package
				checkf(Linker != nullptr, TEXT("BulkData needs its offset fixed on load but no linker found"));
				BulkDataOffset += Linker->Summary.BulkDataStartOffset;
			}
			if (!IsInSeparateFile() && GEventDrivenLoaderEnabled && PackagePath)
			{
				// The BulkDataOffset is written as an offset from the start of the combined PackageHeader+PackageExports+BulkData
				// When EventDrivenLoader is enabled, the combined file is split into a header with linker tables (EPackageSegment::Header)
				// and the exports file (EPackageSegment::Exports) with serialized bytes from exports and with the closing bulk data section (if there is one)
				// We need to modify the BulkDataOffset to be relative to the start of the Exports file
				BulkDataOffset -= IPackageResourceManager::Get().FileSize(*PackagePath, EPackageSegment::Header);
			}

			// Reset the token even though it should already be invalid (it will be set later when registered)
			Data.Token = InvalidToken;
		}

		// Some error handling cases in this function require us to load the data before we return from ::Serialize
		// but it is not safe to do so until the end of this method. By setting this flag to true we can indicate that
		// the load is required.
		bool bShouldForceLoad = false;

		if (IsInlined())
		{
			UE_CLOG(bAttemptFileMapping, LogSerialization, Error, TEXT("Attempt to file map inline bulk data, this will almost certainly fail due to alignment requirements. Package '%s'"), *Package->GetFName().ToString());

			// Inline data is already in the archive so serialize it immediately
			void* DataBuffer = AllocateData(BulkDataSize);
			SerializeBulkData(Ar, DataBuffer, BulkDataSize);

			// Apply the legacy BULKDATA_AlwaysAllowDiscard fix for inline data in some conditions
			ConditionalSetInlineAlwaysAllowDiscard(bPackageUsesIoStore);
		}
		else
		{
			if (IsDuplicateNonOptional())
			{
				ProcessDuplicateData(DuplicateFlags, DuplicateSizeOnDisk, DuplicateOffset, Package, PackagePath, Linker);
			}

			if (bAttemptFileMapping && !IsInSeparateFile() && (bPackageUsesIoStore || !Ar.IsAllowingLazyLoading()))
			{
				UE_CLOG(bAttemptFileMapping, LogSerialization, Error,
					TEXT("Attempt to file map BulkData in end-of-package-file section, this is not supported when %s. Package '%s'"),
					bPackageUsesIoStore ? TEXT("using IoDispatcher") : TEXT("archive does not support lazyload"), *Package->GetFName().ToString());
				bShouldForceLoad = true; // Signal we want to force the BulkData to load
			}
			else if (bAttemptFileMapping)
			{
				if (bPackageUsesIoStore)
				{
					check(IsInSeparateFile());
					TIoStatusOr<FIoMappedRegion> Status = IoDispatcher->OpenMapped(CreateChunkId(), FIoReadOptions());
					if (Status.IsOk())
					{
						FIoMappedRegion MappedRegion = Status.ConsumeValueOrDie();
						DataAllocation.SetMemoryMappedData(this, MappedRegion.MappedFileHandle, MappedRegion.MappedFileRegion);
					}
					else
					{
						bShouldForceLoad = true; // Signal we want to force the BulkData to load
					}
				}
				else
				{
					// If we have no valid input packagepath then the package is broken anyway and we will not be able to find any memory mapped data!
					if (PackagePath != nullptr)
					{
						EPackageSegment PackageSegment = GetPackageSegmentFromFlags();
						if (!MemoryMapBulkData(*PackagePath, PackageSegment, BulkDataOffset, BulkDataSize))
						{
							bShouldForceLoad = true; // Signal we want to force the BulkData to load
						}
					}
				}
			}
			else if (!IsInSeparateFile() && (bPackageUsesIoStore || !Ar.IsAllowingLazyLoading()))
			{
				// If the data is in the end-of-package-file section, and we can't load the package file again later either
				// because we're using the IoDispatcher or because the archive does not support lazy loading then we have
				// to load the data immediately.
				bShouldForceLoad = true;
			}
		}

		// If we have a PackagePath then we need to make sure we can retrieve it later!
		if (PackagePath != nullptr)
		{
			check(Data.Token == InvalidToken);
			Data.Token = PackageTokenSystem::RegisterToken(Package->GetFName(), *PackagePath);
		}

		if (bShouldForceLoad)
		{
			if (!IsInSeparateFile())
			{
				check(!IsInlined()); // We checked for IsInlined up above and already loaded the data in that case
				const int64 CurrentArchiveOffset = Ar.Tell();
				Ar.Seek(BulkDataOffset);

				void* DataBuffer = AllocateData(BulkDataSize);
				SerializeBulkData(Ar, DataBuffer, BulkDataSize);

				Ar.Seek(CurrentArchiveOffset); // Return back to the original point in the archive so future serialization can continue
			}
			else
			{
				ForceBulkDataResident();
			}
		}
	}
#else
	checkf(false, TEXT("FBulkDataBase does not work in the editor")); // Only implemented for cooked builds!
#endif
}

void* FBulkDataBase::Lock(uint32 LockFlags)
{
	checkf(LockStatus == LOCKSTATUS_Unlocked, TEXT("Attempting to lock a BulkData object that is already locked"));

	ForceBulkDataResident(); 	// If nothing is currently loaded then load from disk

	if (LockFlags & LOCK_READ_WRITE)
	{
		checkf(!IsDataMemoryMapped(), TEXT("Attempting to open a write lock on a memory mapped BulkData object, this will not work!"));
		LockStatus = LOCKSTATUS_ReadWriteLock;
		return GetDataBufferForWrite();
	}
	else if (LockFlags & LOCK_READ_ONLY)
	{
		LockStatus = LOCKSTATUS_ReadOnlyLock;
		return (void*)GetDataBufferReadOnly(); // Cast the const away, icky but our hands are tied by the original API at this time
	}
	else
	{
		UE_LOG(LogSerialization, Fatal, TEXT("Unknown lock flag %i"), LockFlags);
		return nullptr;
	}
}

const void* FBulkDataBase::LockReadOnly() const
{
	checkf(LockStatus == LOCKSTATUS_Unlocked, TEXT("Attempting to lock a BulkData object that is already locked"));
	LockStatus = LOCKSTATUS_ReadOnlyLock;

	return GetDataBufferReadOnly();
}

void FBulkDataBase::Unlock() const
{
	checkf(LockStatus != LOCKSTATUS_Unlocked, TEXT("Attempting to unlock a BulkData object that is not locked"));
	LockStatus = LOCKSTATUS_Unlocked;

	// Free pointer if we're guaranteed to only to access the data once.
	if (IsSingleUse())
	{
		// Cast away const so that we can match the original bulkdata api
		// which had ::Unlock as const too.
		const_cast<FBulkDataBase*>(this)->FreeData();
	}
}

bool FBulkDataBase::IsLocked() const
{ 
	return LockStatus != LOCKSTATUS_Unlocked;
}

void* FBulkDataBase::Realloc(int64 SizeInBytes)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FBulkDataBase::Realloc"), STAT_UBD_Realloc, STATGROUP_Memory);

	checkf(LockStatus == LOCKSTATUS_ReadWriteLock, TEXT("BulkData must be locked for 'write' before reallocating!"));
	checkf(!CanLoadFromDisk(), TEXT("Cannot re-allocate a FBulkDataBase object that represents a file on disk!"));

	// We might want to consider this a valid use case if anyone can come up with one?
	checkf(!IsUsingIODispatcher(), TEXT("Attempting to re-allocate data loaded from the IoDispatcher"));

	ReallocateData(SizeInBytes);

	BulkDataSize = SizeInBytes;

	return GetDataBufferForWrite();
}

void FBulkDataBase::GetCopy(void** DstBuffer, bool bDiscardInternalCopy)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FBulkDataBase::GetCopy"), STAT_UBD_GetCopy, STATGROUP_Memory);

	checkf(LockStatus == LOCKSTATUS_Unlocked, TEXT("Attempting to modify a BulkData object that is locked"));
	checkf(DstBuffer, TEXT("FBulkDataBase::GetCopy requires a valid DstBuffer"));	// Really should just use a reference to a pointer 
																					// but we are stuck trying to stick with the old bulkdata API for now

	// Wait for anything that might be currently loading
	FlushAsyncLoading();

	UE_CLOG(IsDataMemoryMapped(), LogSerialization, Warning, TEXT("FBulkDataBase::GetCopy being called on a memory mapped BulkData object, call ::StealFileMapping instead!"));

	if (*DstBuffer != nullptr)
	{
		// TODO: Might be worth changing the API so that we can validate that the buffer is large enough?
		if (IsBulkDataLoaded())
		{
			FMemory::Memcpy(*DstBuffer, GetDataBufferReadOnly(), GetBulkDataSize());

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
				DataAllocation.Swap(this, DstBuffer);
			}
			else
			{
				*DstBuffer = FMemory::Malloc(BulkDataSize, 0);
				FMemory::Memcpy(*DstBuffer, GetDataBufferReadOnly(), BulkDataSize);
			}
		}
		else
		{
			LoadDataDirectly(DstBuffer);
		}
	}
}

FIoChunkId FBulkDataBase::CreateChunkId() const 
{ 
	checkf(IsUsingIODispatcher(), TEXT("Calling ::CreateChunkId on Bulkdata that is not using the IoDispatcher"));
	checkf(IsInSeparateFile(), TEXT("Calling ::CreateChunkId on BulkData that is stored in the package file rather than in a separately loadable file."));

	const EIoChunkType Type =	IsOptional() ? EIoChunkType::OptionalBulkData :
								IsFileMemoryMapped() ? EIoChunkType::MemoryMappedBulkData :
								EIoChunkType::BulkData;

	return CreateIoChunkId(Data.PackageID, 0, Type);
}

void FBulkDataBase::SetBulkDataFlags(uint32 BulkDataFlagsToSet)
{
	BulkDataFlags = EBulkDataFlags(BulkDataFlags | BulkDataFlagsToSet);
}

void FBulkDataBase::ResetBulkDataFlags(uint32 BulkDataFlagsToSet)
{
	BulkDataFlags = (EBulkDataFlags)BulkDataFlagsToSet;
}

void FBulkDataBase::ClearBulkDataFlags(uint32 BulkDataFlagsToClear)
{ 
	BulkDataFlags = EBulkDataFlags(BulkDataFlags & ~BulkDataFlagsToClear);
}

void FBulkDataBase::SetRuntimeBulkDataFlags(uint32 BulkDataFlagsToSet)
{
	checkf(	BulkDataFlagsToSet == BULKDATA_UsesIoDispatcher || 
			BulkDataFlagsToSet == BULKDATA_DataIsMemoryMapped || 
			BulkDataFlagsToSet == BULKDATA_HasAsyncReadPending,
			TEXT("Attempting to set an invalid runtime flag"));
			
	BulkDataFlags = EBulkDataFlags(BulkDataFlags | BulkDataFlagsToSet);
}

void FBulkDataBase::ClearRuntimeBulkDataFlags(uint32 BulkDataFlagsToClear)
{
	checkf(	BulkDataFlagsToClear == BULKDATA_UsesIoDispatcher ||
			BulkDataFlagsToClear == BULKDATA_DataIsMemoryMapped ||
			BulkDataFlagsToClear == BULKDATA_HasAsyncReadPending,
			TEXT("Attempting to clear an invalid runtime flag"));

	BulkDataFlags = EBulkDataFlags(BulkDataFlags & ~BulkDataFlagsToClear);
}

bool FBulkDataBase::NeedsOffsetFixup() const
{
	return (BulkDataFlags & BULKDATA_NoOffsetFixUp) == 0;
}

int64 FBulkDataBase::GetBulkDataSize() const
{
	return BulkDataSize;	
}

bool FBulkDataBase::CanLoadFromDisk() const 
{ 
	// If this BulkData is using the IoDispatcher then it can load from disk 
	if (IsUsingIODispatcher())
	{
		return true;
	}

#if UE_KEEP_INLINE_RELOADING_CONSISTENT
	if (IsInlined())
	{
		return false;
	}
#endif //UE_KEEP_INLINE_RELOADING_CONSISTENT

	// If this BulkData has a fallback token then it can find it's filepath and load from disk
	if (Data.Token != InvalidToken)
	{
		return true;
	}

	return false;
}

bool FBulkDataBase::DoesExist() const
{
#if !ALLOW_OPTIONAL_DATA
	if (IsOptional())
	{
		return false;
	}
#endif
	if (!IsUsingIODispatcher())
	{
		if (Data.Token == InvalidToken)
		{
			return false;
		}
		FPackagePath PackagePath = PackageTokenSystem::GetPackagePath(Data.Token);
		return IPackageResourceManager::Get().DoesPackageExist(PackagePath, GetPackageSegmentFromFlags());
	}
	else
	{
		return IoDispatcher->DoesChunkExist(CreateChunkId());
	}
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
	return (GetBulkDataFlags() & BULKDATA_PayloadAtEndOfFile) == 0;
}

bool FBulkDataBase::IsInSeparateFile() const
{
	return (GetBulkDataFlags() & BULKDATA_PayloadInSeperateFile) != 0;
}

bool FBulkDataBase::IsSingleUse() const
{
	return (BulkDataFlags & BULKDATA_SingleUse) != 0;
}

bool FBulkDataBase::IsFileMemoryMapped() const
{
	return (BulkDataFlags & BULKDATA_MemoryMappedPayload) != 0;
}

bool FBulkDataBase::IsDataMemoryMapped() const
{
	return (BulkDataFlags & BULKDATA_DataIsMemoryMapped) != 0;
}

bool FBulkDataBase::IsUsingIODispatcher() const
{
	return (BulkDataFlags & BULKDATA_UsesIoDispatcher) != 0;
}

IAsyncReadFileHandle* FBulkDataBase::OpenAsyncReadHandle() const
{
	if (IsUsingIODispatcher())
	{
		checkf(IsInSeparateFile(),
			TEXT("Attempting to OpenAsyncReadHandle on %s when the IoDispatcher is enabled, this operation is not supported!"),
			IsInlined() ? TEXT("inline BulkData") : TEXT("BulkData in end-of-package-file section"));
		return UE::BulkData::Private::CreateAsyncReadHandle(CreateChunkId());
	}
	else
	{
		FOpenAsyncPackageResult OpenResult = IPackageResourceManager::Get().OpenAsyncReadPackage(GetPackagePath(), GetPackageSegmentFromFlags());
		return OpenResult.Handle.Release();
	}
}

IBulkDataIORequest* FBulkDataBase::CreateStreamingRequest(EAsyncIOPriorityAndFlags Priority, FBulkDataIORequestCallBack* CompleteCallback, uint8* UserSuppliedMemory) const
{
	const int64 DataSize = GetBulkDataSize();

	return CreateStreamingRequest(0, DataSize, Priority, CompleteCallback, UserSuppliedMemory);
}

IBulkDataIORequest* FBulkDataBase::CreateStreamingRequest(int64 OffsetInBulkData, int64 BytesToRead, EAsyncIOPriorityAndFlags Priority, FBulkDataIORequestCallBack* CompleteCallback, uint8* UserSuppliedMemory) const
{
#if !UE_KEEP_INLINE_RELOADING_CONSISTENT	
	// Note that we only want the ensure to trigger if we have a valid offset (the bulkdata references data from disk)
	ensureMsgf(	!IsInlined() || BulkDataOffset == INDEX_NONE || ShouldIgnoreInlineDataReloadEnsures(),
				TEXT("Attempting to stream inline BulkData! This operation is not supported by the IoDispatcher and so will eventually stop working."
				" The calling code should be fixed to retain the inline data in memory and re-use it rather than discard it and then try to reload from disk!"));
#endif //!UE_KEEP_INLINE_RELOADING_CONSISTENT

	if (!CanLoadFromDisk())
	{
		UE_LOG(LogSerialization, Error, TEXT("Attempting to stream a BulkData object that cannot be loaded from disk"));
		return nullptr;
	}

	if (IsUsingIODispatcher())
	{
		checkf(OffsetInBulkData + BytesToRead <= BulkDataSize, TEXT("Attempting to read past the end of BulkData"));
		checkf(IsInSeparateFile(),
			TEXT("Attempting to CreateStreamingRequest on %s when the IoDispatcher is enabled, this operation is not supported!"),
			IsInlined() ? TEXT("inline BulkData") : TEXT("BulkData in end-of-package-file section"));
		FBulkDataIoDispatcherRequest* BulkDataIoDispatcherRequest = new FBulkDataIoDispatcherRequest(CreateChunkId(), BulkDataOffset + OffsetInBulkData, BytesToRead, ConvertToIoDispatcherPriority(Priority), CompleteCallback, UserSuppliedMemory);
		BulkDataIoDispatcherRequest->StartAsyncWork();

		return BulkDataIoDispatcherRequest;
	}
	else
	{
		FPackagePath PackagePath = PackageTokenSystem::GetPackagePath(Data.Token);

		EPackageSegment PackageSegment = GetPackageSegmentFromFlags();

		UE_CLOG(IsStoredCompressedOnDisk(), LogSerialization, Fatal, TEXT("Package level compression is no longer supported (%s)."), *PackagePath.GetDebugName(PackageSegment));
		UE_CLOG(BulkDataSize <= 0, LogSerialization, Error, TEXT("(%s) has invalid bulk data size."), *PackagePath.GetDebugName(PackageSegment));

		FOpenAsyncPackageResult OpenResult = IPackageResourceManager::Get().OpenAsyncReadPackage(PackagePath, PackageSegment);
		IAsyncReadFileHandle* IORequestHandle = OpenResult.Handle.Release();
		checkf(IORequestHandle, TEXT("OpenAsyncRead failed")); // An assert as there shouldn't be a way for this to fail

		if (IORequestHandle == nullptr)
		{
			return nullptr;
		}

		const int64 OffsetInFile = BulkDataOffset + OffsetInBulkData;

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
	checkf(RangeArray.Num() > 0, TEXT("RangeArray cannot be empty"));

	const FBulkDataBase& Start = *(RangeArray[0]);

	checkf(!Start.IsInlined(), TEXT("Cannot stream inlined BulkData"));

	if (Start.IsUsingIODispatcher())
	{
		const FBulkDataBase& End = *RangeArray[RangeArray.Num() - 1];

		checkf(Start.IsInSeparateFile(),
			TEXT("Attempting to CreateStreamingRequestForRange on %s when the IoDispatcher is enabled, this operation is not supported!"),
			Start.IsInlined() ? TEXT("inline BulkData") : TEXT("BulkData in end-of-package-file section"));
		checkf(End.IsInSeparateFile() && Start.CreateChunkId() == End.CreateChunkId(), TEXT("BulkData range does not come from the same file (%s vs %s)"),
			*Start.GetPackagePath().GetDebugName(Start.GetPackageSegment()), *End.GetPackagePath().GetDebugName(End.GetPackageSegment()));

		const int64 ReadOffset = Start.GetBulkDataOffsetInFile();
		const int64 ReadLength = (End.GetBulkDataOffsetInFile() + End.GetBulkDataSize()) - ReadOffset;

		checkf(ReadLength > 0, TEXT("Read length is 0"));

		FBulkDataIoDispatcherRequest* IoRequest = new FBulkDataIoDispatcherRequest(Start.CreateChunkId(), ReadOffset, ReadLength, ConvertToIoDispatcherPriority(Priority), CompleteCallback, nullptr);
		IoRequest->StartAsyncWork();

		return IoRequest;
	}
	else
	{	
		const FBulkDataBase& End = *RangeArray[RangeArray.Num() - 1];

		checkf(Start.GetPackagePath() == End.GetPackagePath(), TEXT("BulkData range does not come from the same file (%s vs %s)"),
			*Start.GetPackagePath().GetDebugName(), *End.GetPackagePath().GetDebugName());

		const int64 ReadOffset = Start.GetBulkDataOffsetInFile();
		const int64 ReadLength = (End.GetBulkDataOffsetInFile() + End.GetBulkDataSize()) - ReadOffset;

		checkf(ReadLength > 0, TEXT("Read length is 0"));

		return Start.CreateStreamingRequest(0, ReadLength, Priority, CompleteCallback, nullptr);
	}
}

void FBulkDataBase::ForceBulkDataResident()
{
	// First wait for any async load requests to finish
	FlushAsyncLoading();

	// Then check if we actually need to load or not
	if (!IsBulkDataLoaded())
	{
		void* DataBuffer = nullptr;
		LoadDataDirectly(&DataBuffer);

		DataAllocation.SetData(this, DataBuffer);
	}
}

FOwnedBulkDataPtr* FBulkDataBase::StealFileMapping()
{
	checkf(LockStatus == LOCKSTATUS_Unlocked, TEXT("Attempting to modify a BulkData object that is locked"));

	return DataAllocation.StealFileMapping(this);
}

void FBulkDataBase::RemoveBulkData()
{
	checkf(LockStatus == LOCKSTATUS_Unlocked, TEXT("Attempting to modify a BulkData object that is locked"));

	FreeData();

	if (!IsUsingIODispatcher())
	{
		PackageTokenSystem::UnRegisterToken(Data.Token);
	}	

	Data.Token = InvalidToken;
}

bool FBulkDataBase::StartAsyncLoading()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FUntypedBulkData::StartAsyncLoading"), STAT_UBD_StartSerializingBulkData, STATGROUP_Memory);

	if (!IsAsyncLoadingComplete())
	{
		return true; // Early out if an asynchronous load is already in progress.
	}

	if (IsBulkDataLoaded())
	{
		return false; // Early out if we do not need to actually load any data
	}

	if (!CanLoadFromDisk())
	{
		return false; // Early out if we cannot load from disk
	}

	checkf(LockStatus == LOCKSTATUS_Unlocked, TEXT("Attempting to modify a BulkData object that is locked"));

	LockStatus = LOCKSTATUS_ReadWriteLock; // Bulkdata is effectively locked while streaming!

	// Indicate that we have an async read in flight
	SetRuntimeBulkDataFlags(BULKDATA_HasAsyncReadPending);
	FPlatformMisc::MemoryBarrier();

	AsyncCallback Callback = [this](TIoStatusOr<FIoBuffer> Result)
	{
		CHECK_IOSTATUS(Result.Status(), TEXT("FBulkDataBase::StartAsyncLoading"));
		FIoBuffer IoBuffer = Result.ConsumeValueOrDie();

		// It is assumed that ::LoadDataAsynchronously will allocate memory for the loaded data, so that we
		// do not need to take ownership here. This should guard against any future change in functionality.
		checkf(!IoBuffer.IsMemoryOwned(), TEXT("The loaded data is not owned by the BulkData object"));
		DataAllocation.SetData(this, IoBuffer.Data());

		FPlatformMisc::MemoryBarrier();

		ClearRuntimeBulkDataFlags(BULKDATA_HasAsyncReadPending);
		LockStatus = LOCKSTATUS_Unlocked;
	};

	LoadDataAsynchronously(MoveTemp(Callback));

	return true;
}

bool FBulkDataBase::IsAsyncLoadingComplete() const
{
	return (GetBulkDataFlags() & BULKDATA_HasAsyncReadPending) == 0;
}

int64 FBulkDataBase::GetBulkDataOffsetInFile() const
{
	return BulkDataOffset;
}

FIoFilenameHash FBulkDataBase::GetIoFilenameHash() const
{
	if (IsUsingIODispatcher())
	{
		checkf(IsInSeparateFile(),
			TEXT("Attempting to GetIoFilenameHash on %s when the IoDispatcher is enabled, this operation is not supported!"),
			IsInlined() ? TEXT("inline BulkData") : TEXT("BulkData in end-of-package-file section"));
		return MakeIoFilenameHash(CreateChunkId());
	}
	else
	{
		return MakeIoFilenameHash(PackageTokenSystem::GetPackagePath(Data.Token));
	}
}

FString FBulkDataBase::GetFilename() const
{
	return GetPackagePath().GetLocalFullPath(GetPackageSegmentFromFlags());
}

FPackagePath FBulkDataBase::GetPackagePath() const
{
	if (!IsUsingIODispatcher())
	{
		return PackageTokenSystem::GetPackagePath(Data.Token);
	}
	else
	{
		UE_LOG(LogBulkDataRuntime, Warning, TEXT("Attempting to get the PackagePath for BulkData that uses the IoDispatcher, this will return an empty PackagePath"));
		return FPackagePath();
	}
}

EPackageSegment FBulkDataBase::GetPackageSegment() const
{
	return GetPackageSegmentFromFlags();
}

bool FBulkDataBase::CanDiscardInternalData() const
{	
	// Data marked as single use should always be discarded
	if (IsSingleUse())
	{
		return true;
	}

	// If we can load from disk then we can discard it as it can be reloaded later
	if (CanLoadFromDisk())
	{
		return true;
	}

	// If BULKDATA_AlwaysAllowDiscard has been set then we should always allow the data to 
	// be discarded even if it cannot be reloaded again.
	if ((BulkDataFlags & BULKDATA_AlwaysAllowDiscard) != 0)
	{
		return true;
	}

	return false;
}

void FBulkDataBase::LoadDataDirectly(void** DstBuffer)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FBulkDataBase::LoadDataDirectly"), STAT_UBD_LoadDataDirectly, STATGROUP_Memory);
	
	// Early out if we have no data to load
	if (GetBulkDataSize() == 0)
	{
		return;
	}

#if !UE_KEEP_INLINE_RELOADING_CONSISTENT	
	// Note that we only want the ensure to trigger if we have a valid offset (the bulkdata references data from disk)
	ensureMsgf(!IsInlined() || BulkDataOffset == INDEX_NONE || ShouldIgnoreInlineDataReloadEnsures(), 
			TEXT(	"Attempting to reload inline BulkData! This operation is not supported by the IoDispatcher and so will eventually stop working."
					" The calling code should be fixed to retain the inline data in memory and re-use it rather than discard it and then try to reload from disk!"));
#endif //!UE_KEEP_INLINE_RELOADING_CONSISTENT

	if (!CanLoadFromDisk())
	{
		UE_LOG(LogSerialization, Error, TEXT("Attempting to load a BulkData object that cannot be loaded from disk"));
		return;
	}

	if (IsUsingIODispatcher())
	{
		InternalLoadFromIoStore(DstBuffer);
	}
	else
	{
		InternalLoadFromPackageResource(DstBuffer);
	}
}

void FBulkDataBase::LoadDataAsynchronously(AsyncCallback&& Callback)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FBulkDataBase::LoadDataDirectly"), STAT_UBD_LoadDataDirectly, STATGROUP_Memory);
	
	// Early out if we have no data to load
	if (GetBulkDataSize() == 0)
	{
		return;
	}

#if !UE_KEEP_INLINE_RELOADING_CONSISTENT
	ensureMsgf(!IsInlined() || ShouldIgnoreInlineDataReloadEnsures(), 
				TEXT(	"Attempting to reload inline BulkData! This operation is not supported by the IoDispatcher and so will eventually stop working."
						" The calling code should be fixed to retain the inline data in memory and re-use it rather than discard it and then try to reload from disk!"));
#endif //!UE_KEEP_INLINE_RELOADING_CONSISTENT

	if (!CanLoadFromDisk())
	{
		UE_LOG(LogSerialization, Error, TEXT("Attempting to load a BulkData object that cannot be loaded from disk"));
		return; // Early out if there is nothing to load anyway
	}

	if (IsUsingIODispatcher())
	{
		void* DummyPointer = nullptr;
		InternalLoadFromIoStoreAsync(&DummyPointer, MoveTemp(Callback));
	}
	else
	{
		Async(EAsyncExecution::ThreadPool, [this, Callback]()
			{
				void* DataPtr = nullptr;
				InternalLoadFromPackageResource(&DataPtr);

				FIoBuffer Buffer(FIoBuffer::Wrap, DataPtr, GetBulkDataSize());
				TIoStatusOr<FIoBuffer> Status(Buffer);

				Callback(Status);
			});
	}
}

void FBulkDataBase::InternalLoadFromPackageResource(void** DstBuffer)
{
	FPackagePath PackagePath = PackageTokenSystem::GetPackagePath(Data.Token);
	EPackageSegment PackageSegment = GetPackageSegmentFromFlags();

	// If the data is inlined then we already loaded it during ::Serialize, this warning should help track cases where data is being discarded then re-requested.
	UE_CLOG(IsInlined(), LogSerialization, Warning, TEXT("Reloading inlined bulk data directly from disk, this is detrimental to loading performance. PackagePath: '%s'."),
		*PackagePath.GetDebugName(PackageSegment));

	FOpenPackageResult Result = IPackageResourceManager::Get().OpenReadPackage(PackagePath, PackageSegment);
	checkf(Result.Archive.IsValid() && Result.Format == EPackageFormat::Binary, TEXT("Failed to open the file to load bulk data from. PackagePath: '%s': %s."),
		*PackagePath.GetDebugName(PackageSegment), (!Result.Archive.IsValid() ? TEXT("could not find package") : TEXT("package is a TextAsset which is not supported")));

	// Seek to the beginning of the bulk data in the file.
	Result.Archive->Seek(BulkDataOffset);

	if (*DstBuffer == nullptr)
	{
		*DstBuffer = FMemory::Malloc(BulkDataSize, 0);
	}

	SerializeBulkData(*Result.Archive, *DstBuffer, BulkDataSize);
}

void FBulkDataBase::InternalLoadFromIoStore(void** DstBuffer)
{
	// Allocate the buffer if needed
	if (*DstBuffer == nullptr)
	{
		*DstBuffer = FMemory::Malloc(GetBulkDataSize(), 0);
	}

	// Set up our options (we only need to set the target)
	FIoReadOptions Options(BulkDataOffset, BulkDataSize);
	
	// If we do not need to uncompress the data we can load it directly to the destination buffer
	if (!IsStoredCompressedOnDisk()) 
	{
		Options.SetTargetVa(*DstBuffer);
	}	

	FIoBatch Batch = GetIoDispatcher()->NewBatch();
	FIoRequest Request = Batch.Read(CreateChunkId(), Options, IoDispatcherPriority_High);

	FEvent* BatchCompletedEvent = FPlatformProcess::GetSynchEventFromPool();
	Batch.IssueAndTriggerEvent(BatchCompletedEvent);
	BatchCompletedEvent->Wait(); // Blocking wait until all requests in the batch are done
	FPlatformProcess::ReturnSynchEventToPool(BatchCompletedEvent);
	CHECK_IOSTATUS(Request.Status(), TEXT("FIoRequest"));

	// If the data is compressed we need to decompress it to the destination buffer.
	// We know it was compressed via FArchive so the easiest way to decompress it 
	// is to wrap it in a memory reader archive.
	if (IsStoredCompressedOnDisk())
	{
		const FIoBuffer& CompressedBuffer = Request.GetResultOrDie();
		FLargeMemoryReader Ar(CompressedBuffer.Data(), (int64)CompressedBuffer.DataSize());
		Ar.SerializeCompressed(*DstBuffer, GetBulkDataSize(), GetDecompressionFormat(), COMPRESS_NoFlags, false);
	}
}

void FBulkDataBase::InternalLoadFromIoStoreAsync(void** DstBuffer, AsyncCallback&& Callback)
{
	// Allocate the buffer if needed
	if (*DstBuffer == nullptr)
	{
		*DstBuffer = FMemory::Malloc(GetBulkDataSize(), 0);
	}

	// Set up our options (we only need to set the target)
	FIoReadOptions Options;
	Options.SetRange(GetBulkDataOffsetInFile(), GetBulkDataSize());
	Options.SetTargetVa(*DstBuffer);

	FIoBatch Batch = GetIoDispatcher()->NewBatch();
	Batch.ReadWithCallback(CreateChunkId(), Options, IoDispatcherPriority_Low, MoveTemp(Callback));
	Batch.Issue();
}

void FBulkDataBase::ProcessDuplicateData(EBulkDataFlags NewFlags, int64 NewSizeOnDisk, int64 NewOffset,
	const UPackage* Package, const FPackagePath* PackagePath, const FLinkerLoad* Linker)
{
	// We need to load the optional bulkdata info as we might need to create a FIoChunkId based on it!
#if ALLOW_OPTIONAL_DATA
	bool bUseOptionalSegment = false;
	bool bUsingIODispatcher = IsUsingIODispatcher();
	if (bUsingIODispatcher)
	{
		const FIoChunkId OptionalChunkId = CreateIoChunkId(Data.PackageID, 0, EIoChunkType::OptionalBulkData);
		bUseOptionalSegment = IoDispatcher->DoesChunkExist(OptionalChunkId);
	}
	else
	{
		// If we have no valid input packagepath then the package is broken anyway and we will not be able to find the optional data!
		bUseOptionalSegment = PackagePath != nullptr && 
			IPackageResourceManager::Get().DoesPackageExist(*PackagePath, EPackageSegment::BulkDataOptional);
	}

	if (bUseOptionalSegment)
	{
		checkf(BulkDataSize == NewSizeOnDisk, TEXT("Size mismatch between original data size (%lld)and duplicate data size (%lld)"), BulkDataSize, NewSizeOnDisk);
		BulkDataOffset = NewOffset;
		BulkDataFlags = static_cast<EBulkDataFlags>((NewFlags & ~BULKDATA_DuplicateNonOptionalPayload)
			| BULKDATA_OptionalPayload | BULKDATA_PayloadInSeperateFile | BULKDATA_PayloadAtEndOfFile);
		if (bUsingIODispatcher)
		{
			checkf(!NeedsOffsetFixup(), TEXT("IODispatcher does not support offset fixups; SaveBulkData during cooking should have added the flag BULKDATA_NoOffsetFixUp"));
			BulkDataFlags = static_cast<EBulkDataFlags>(BulkDataFlags | BULKDATA_UsesIoDispatcher);
		}
		else
		{
			if (NeedsOffsetFixup())
			{
				checkf(Linker != nullptr, TEXT("BulkData needs its offset fixed on load but no linker found"));
				BulkDataOffset += Linker->Summary.BulkDataStartOffset;
			}
		}
	}
#endif
}

void FBulkDataBase::SerializeDuplicateData(FArchive& Ar, EBulkDataFlags& OutBulkDataFlags, int64& OutBulkDataSizeOnDisk, int64& OutBulkDataOffsetInFile)
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

	if ((OutBulkDataFlags & BULKDATA_BadDataVersion) != 0)
	{
		uint16 DummyBulkDataIndex = InvalidBulkDataIndex;
		Ar << DummyBulkDataIndex;
	}
}

void FBulkDataBase::SerializeBulkData(FArchive& Ar, void* DstBuffer, int64 DataLength)
{	
	checkf(Ar.IsLoading(), TEXT("BulkData2 only supports serialization for loading"));

	if (IsAvailableForUse()) // skip serializing of unused data
	{
		return;
	}

	// Skip serialization for bulk data of zero length
	if (DataLength == 0)
	{
		return;
	}

	checkf(DstBuffer != nullptr, TEXT("No destination buffer was provided for serialization"));

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

bool FBulkDataBase::MemoryMapBulkData(const FPackagePath& PackagePath, EPackageSegment PackageSegment, int64 OffsetInBulkData, int64 BytesToRead)
{
	checkf(!IsBulkDataLoaded(), TEXT("Attempting to memory map BulkData that is already loaded"));

	IMappedFileHandle* MappedHandle = nullptr;
	IMappedFileRegion* MappedRegion = nullptr;

	MappedHandle = IPackageResourceManager::Get().OpenMappedHandleToPackage(PackagePath, PackageSegment);

	if (MappedHandle == nullptr)
	{
		return false;
	}

	MappedRegion = MappedHandle->MapRegion(OffsetInBulkData, BytesToRead, true);
	if (MappedRegion == nullptr)
	{
		delete MappedHandle;
		MappedHandle = nullptr;
		return false;
	}

	checkf(MappedRegion->GetMappedSize() == BytesToRead, TEXT("Mapped size (%lld) is different to the requested size (%lld)!"), MappedRegion->GetMappedSize(), BytesToRead);
	checkf(IsAligned(MappedRegion->GetMappedPtr(), FPlatformProperties::GetMemoryMappingAlignment()), TEXT("Memory mapped file has the wrong alignment!"));

	DataAllocation.SetMemoryMappedData(this, MappedHandle, MappedRegion);

	return true;
}

void FBulkDataBase::FlushAsyncLoading()
{
	if (!IsAsyncLoadingComplete())
	{
		DECLARE_SCOPE_CYCLE_COUNTER(TEXT(" FBulkDataBase::FlushAsyncLoading"), STAT_UBD_WaitForAsyncLoading, STATGROUP_Memory);

#if NO_LOGGING
		while (IsAsyncLoadingComplete() == false)
		{
			FPlatformProcess::Sleep(0);
		}
#else
		uint64 StartTime = FPlatformTime::Cycles64();
		while (IsAsyncLoadingComplete() == false)
		{
			if (FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - StartTime))
			{
				UE_LOG(LogSerialization, Warning, TEXT("Waiting for %s bulk data (%lld) to be loaded longer than 1000ms"),
					*GetPackagePath().GetDebugName(), GetBulkDataSize());
				StartTime = FPlatformTime::Cycles64(); // Reset so we spam the log every second or so that we are stalled!
			}

			FPlatformProcess::Sleep(0);
		}
#endif
	}	
}

EPackageSegment FBulkDataBase::GetPackageSegmentFromFlags() const
{
	if (!IsInSeparateFile())
	{
		if (GEventDrivenLoaderEnabled)
		{
			// With EDL data, packages are split into EPackageSegment::Header (summary and linker tables) and
			// EPackageSegment::Exports (serialized UObject bytes and the bulk data section)
			// Inline and end-of-file bulk data is in the Exports section
			return EPackageSegment::Exports;
		}
		else
		{
			// Without EDL, there is only a single combined file for Summary,linkertables,exports,and end-of-file bulk data
			return EPackageSegment::Header;
		}
	}
	else if (IsOptional())
	{
		return EPackageSegment::BulkDataOptional;
	}
	else if (IsFileMemoryMapped())
	{
		return EPackageSegment::BulkDataMemoryMapped;
	}
	else
	{
		return EPackageSegment::BulkDataDefault;
	}
}

void FBulkDataAllocation::Free(FBulkDataBase* Owner)
{
	if (!Owner->IsDataMemoryMapped())
	{
		FMemory::Free(Allocation.RawData); //-V611 We know this was allocated via FMemory::Malloc if ::IsDataMemoryMapped returned false
		Allocation.RawData = nullptr;
	}
	else
	{
		delete Allocation.MemoryMappedData;
		Allocation.MemoryMappedData = nullptr;
	}
}

void* FBulkDataAllocation::AllocateData(FBulkDataBase* Owner, SIZE_T SizeInBytes)
{
	checkf(Allocation.RawData == nullptr, TEXT("Trying to allocate a BulkData object without freeing it first!"));

	Allocation.RawData = FMemory::Malloc(SizeInBytes, DEFAULT_ALIGNMENT);

	return Allocation.RawData;
}

void* FBulkDataAllocation::ReallocateData(FBulkDataBase* Owner, SIZE_T SizeInBytes)
{
	checkf(!Owner->IsDataMemoryMapped(),  TEXT("Trying to reallocate a memory mapped BulkData object without freeing it first!"));

	Allocation.RawData = FMemory::Realloc(Allocation.RawData, SizeInBytes, DEFAULT_ALIGNMENT);

	return Allocation.RawData;
}

void FBulkDataAllocation::SetData(FBulkDataBase* Owner, void* Buffer)
{
	checkf(Allocation.RawData == nullptr, TEXT("Trying to assign a BulkData object without freeing it first!"));

	Allocation.RawData = Buffer;
}

void FBulkDataAllocation::SetMemoryMappedData(FBulkDataBase* Owner, IMappedFileHandle* MappedHandle, IMappedFileRegion* MappedRegion)
{
	checkf(Allocation.MemoryMappedData == nullptr, TEXT("Trying to assign a BulkData object without freeing it first!"));
	Allocation.MemoryMappedData = new FOwnedBulkDataPtr(MappedHandle, MappedRegion);

	Owner->SetRuntimeBulkDataFlags(BULKDATA_DataIsMemoryMapped);
}

void* FBulkDataAllocation::GetAllocationForWrite(const FBulkDataBase* Owner) const
{
	if (!Owner->IsDataMemoryMapped())
	{
		return Allocation.RawData;
	}
	else
	{
		return nullptr;
	}
}

const void* FBulkDataAllocation::GetAllocationReadOnly(const FBulkDataBase* Owner) const
{
	if (!Owner->IsDataMemoryMapped())
	{
		return Allocation.RawData;
	}
	else if (Allocation.MemoryMappedData != nullptr)
	{
		return Allocation.MemoryMappedData->GetPointer();
	}
	else
	{
		return nullptr;
	}
}

FOwnedBulkDataPtr* FBulkDataAllocation::StealFileMapping(FBulkDataBase* Owner)
{
	FOwnedBulkDataPtr* Ptr;
	if (!Owner->IsDataMemoryMapped())
	{
		Ptr = new FOwnedBulkDataPtr(Allocation.RawData);
	}
	else
	{
		Ptr = Allocation.MemoryMappedData;
		Owner->ClearRuntimeBulkDataFlags(BULKDATA_DataIsMemoryMapped);
	}	

	Allocation.RawData = nullptr;

	return Ptr;
}

void FBulkDataAllocation::Swap(FBulkDataBase* Owner, void** DstBuffer)
{
	if (!Owner->IsDataMemoryMapped())
	{
		::Swap(*DstBuffer, Allocation.RawData);
	}
	else
	{
		const int64 BulkDataSize = Owner->GetBulkDataSize();

		*DstBuffer = FMemory::Malloc(BulkDataSize, DEFAULT_ALIGNMENT);
		FMemory::Memcpy(*DstBuffer, Allocation.MemoryMappedData->GetPointer(), BulkDataSize);

		delete Allocation.MemoryMappedData;
		Allocation.MemoryMappedData = nullptr;

		Owner->ClearRuntimeBulkDataFlags(BULKDATA_DataIsMemoryMapped);
	}	
}

#undef CHECK_IOSTATUS
