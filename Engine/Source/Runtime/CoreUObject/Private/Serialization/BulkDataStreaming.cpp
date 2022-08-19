// Copyright Epic Games, Inc. All Rights Reserved.
#include "Serialization/BulkData.h"
#include "Algo/AllOf.h"
#include "Async/MappedFileHandle.h"
#include "Experimental/Async/LazyEvent.h"
#include "HAL/CriticalSection.h"
#include "Misc/Timespan.h"
#include "Serialization/MemoryReader.h"
#include "UObject/PackageResourceManager.h"

//////////////////////////////////////////////////////////////////////////////

FBulkDataIORequest::FBulkDataIORequest(IAsyncReadFileHandle* InFileHandle)
	: FileHandle(InFileHandle)
	, ReadRequest(nullptr)
	, Size(INDEX_NONE)
{
}

FBulkDataIORequest::FBulkDataIORequest(IAsyncReadFileHandle* InFileHandle, IAsyncReadRequest* InReadRequest, int64 BytesToRead)
	: FileHandle(InFileHandle)
	, ReadRequest(InReadRequest)
	, Size(BytesToRead)
{

}

FBulkDataIORequest::~FBulkDataIORequest()
{
	delete ReadRequest;
	delete FileHandle;
}

bool FBulkDataIORequest::MakeReadRequest(int64 Offset, int64 BytesToRead, EAsyncIOPriorityAndFlags PriorityAndFlags, FBulkDataIORequestCallBack* CompleteCallback, uint8* UserSuppliedMemory)
{
	check(ReadRequest == nullptr);

	FBulkDataIORequestCallBack LocalCallback = *CompleteCallback;
	FAsyncFileCallBack AsyncFileCallBack = [LocalCallback, BytesToRead, this](bool bWasCancelled, IAsyncReadRequest* InRequest)
	{
		// In some cases the call to ReadRequest can invoke the callback immediately (if the requested data is cached 
		// in the pak file system for example) which means that FBulkDataIORequest::ReadRequest might not actually be
		// set correctly, so we need to make sure it is assigned before we invoke LocalCallback!
		ReadRequest = InRequest;

		Size = BytesToRead;
		LocalCallback(bWasCancelled, this);
	};

	ReadRequest = FileHandle->ReadRequest(Offset, BytesToRead, PriorityAndFlags, &AsyncFileCallBack, UserSuppliedMemory);
	
	if (ReadRequest != nullptr)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool FBulkDataIORequest::PollCompletion() const
{
	return ReadRequest->PollCompletion();
}

bool FBulkDataIORequest::WaitCompletion(float TimeLimitSeconds)
{
	return ReadRequest->WaitCompletion(TimeLimitSeconds);
}

uint8* FBulkDataIORequest::GetReadResults()
{
	return ReadRequest->GetReadResults();
}

int64 FBulkDataIORequest::GetSize() const
{
	return Size;
}

void FBulkDataIORequest::Cancel()
{
	ReadRequest->Cancel();
}

namespace UE::BulkData::Private
{

//////////////////////////////////////////////////////////////////////////////

FIoChunkId CreateBulkDataIoChunkId(const FBulkMetaData& BulkMeta, const FPackageId& PackageId)
{
	if (PackageId.IsValid() == false)
	{
		return FIoChunkId();
	}

	const EBulkDataFlags BulkDataFlags = BulkMeta.GetFlags();

	const EIoChunkType ChunkType = BulkDataFlags & BULKDATA_OptionalPayload
		? EIoChunkType::OptionalBulkData
		: BulkDataFlags & BULKDATA_MemoryMappedPayload
			? EIoChunkType::MemoryMappedBulkData
			: EIoChunkType::BulkData;

	const uint16 ChunkIndex = EnumHasAnyFlags(BulkMeta.GetMetaFlags(), FBulkMetaData::EMetaFlags::OptionalPackage) ? 1 : 0;
	return CreateIoChunkId(PackageId.Value(), ChunkIndex, ChunkType);
}

//////////////////////////////////////////////////////////////////////////////

EPackageSegment GetPackageSegmentFromFlags(const FBulkMetaData& BulkMeta)
{
	const EBulkDataFlags BulkDataFlags = BulkMeta.GetFlags();

	if ((BulkDataFlags & BULKDATA_PayloadInSeperateFile) == 0)
	{
		const bool bLoadingFromCookedPackage = EnumHasAnyFlags(BulkMeta.GetMetaFlags(), FBulkMetaData::EMetaFlags::CookedPackage);
		if (bLoadingFromCookedPackage)
		{
			// Cooked packages are split into EPackageSegment::Header (summary and linker tables) and
			// EPackageSegment::Exports (serialized UObject bytes and the bulk data section)
			// Inline and end-of-file bulk data is in the Exports section
			return EPackageSegment::Exports;
		}
		else
		{
			return EPackageSegment::Header;
		}
	}
	else if (BulkDataFlags & BULKDATA_OptionalPayload )
	{
		return EPackageSegment::BulkDataOptional;
	}
	else if (BulkDataFlags & BULKDATA_MemoryMappedPayload)
	{
		return EPackageSegment::BulkDataMemoryMapped;
	}
	else
	{
		return EPackageSegment::BulkDataDefault;
	}
}

//////////////////////////////////////////////////////////////////////////////

enum class EChunkRequestStatus : uint32
{
	None				= 0,
	Pending				= 1 << 0,
	Canceled			= 1 << 1,
	DataReady			= 1 << 2,
	CallbackTriggered	= 1 << 3,
};
ENUM_CLASS_FLAGS(EChunkRequestStatus);

class FChunkRequest
{
public:
	virtual ~FChunkRequest();
	
	void Issue(FIoChunkId ChunkId, FIoReadOptions Options, int32 Priority);

protected:
	FChunkRequest(FIoBuffer&& InBuffer);
	
	inline EChunkRequestStatus GetStatus() const
	{
		return static_cast<EChunkRequestStatus>(Status.load(std::memory_order_consume));
	}

	virtual void HandleChunkResult(TIoStatusOr<FIoBuffer>&& Result) = 0;
	bool WaitForChunkRequest(float TimeLimitSeconds = 0.0f);
	void CancelChunkRequest();
	int64 GetSizeResult() const { return SizeResult; }
	
	FIoBuffer			Buffer;

private:

	UE::FLazyEvent		DoneEvent;
	FIoRequest			Request;
	int64				SizeResult;
	std::atomic<uint32>	Status;
};

FChunkRequest::FChunkRequest(FIoBuffer&& InBuffer)
	: Buffer(MoveTemp(InBuffer))
	, DoneEvent(EEventMode::ManualReset)
	, SizeResult(-1)
	, Status{uint32(EChunkRequestStatus::None)}
{
}

FChunkRequest::~FChunkRequest()
{
	DoneEvent.Wait();
}

void FChunkRequest::Issue(FIoChunkId ChunkId, FIoReadOptions Options, int32 Priority)
{
	Status.store(uint32(EChunkRequestStatus::Pending), std::memory_order_release); 

	check(Options.GetSize() == Buffer.GetSize());
	Options.SetTargetVa(Buffer.GetData());

	FIoBatch IoBatch = FIoDispatcher::Get().NewBatch();
	Request = IoBatch.ReadWithCallback(ChunkId, Options, Priority, [this](TIoStatusOr<FIoBuffer> Result)
	{
		EChunkRequestStatus ReadyOrCanceled = EChunkRequestStatus::Canceled;

		if (Result.IsOk())
		{
			SizeResult = Result.ValueOrDie().GetSize();
			ReadyOrCanceled = EChunkRequestStatus::DataReady;
		}

		Status.store(uint32(ReadyOrCanceled), std::memory_order_release); 
		HandleChunkResult(MoveTemp(Result));
		Status.store(uint32(ReadyOrCanceled | EChunkRequestStatus::CallbackTriggered), std::memory_order_release); 

		DoneEvent.Trigger();
	});

	IoBatch.Issue();
}

bool FChunkRequest::WaitForChunkRequest(float TimeLimitSeconds)
{
	checkf(GetStatus() != EChunkRequestStatus::None, TEXT("The request must be issued before waiting for completion"));

	const uint32 TimeLimitMilliseconds = TimeLimitSeconds <= 0.0f ? MAX_uint32 : (uint32)(TimeLimitSeconds * 1000.0f);
	return DoneEvent.Wait(TimeLimitMilliseconds);
}

void FChunkRequest::CancelChunkRequest()
{
	checkf(GetStatus() != EChunkRequestStatus::None, TEXT("The request must be issued before it can be canceled"));

	uint32 Expected = uint32(EChunkRequestStatus::Pending);
	if (Status.compare_exchange_strong(Expected, uint32(EChunkRequestStatus::Canceled)))
	{
		Request.Cancel();
	}
}

//////////////////////////////////////////////////////////////////////////////

class FChunkReadFileRequest final : public FChunkRequest, public IAsyncReadRequest
{
public:
	FChunkReadFileRequest(FAsyncFileCallBack* Callback, FIoBuffer&& InBuffer);
	virtual ~FChunkReadFileRequest();
	
	virtual void WaitCompletionImpl(float TimeLimitSeconds) override;
	virtual void CancelImpl() override;
	virtual void HandleChunkResult(TIoStatusOr<FIoBuffer>&& Result) override;
};

FChunkReadFileRequest::FChunkReadFileRequest(FAsyncFileCallBack* Callback, FIoBuffer&& InBuffer)
	: FChunkRequest(MoveTemp(InBuffer))
	, IAsyncReadRequest(Callback, false, nullptr)
{
	Memory = Buffer.GetData();
}

FChunkReadFileRequest::~FChunkReadFileRequest()
{
	WaitForChunkRequest();

	// Calling GetReadResult transfers ownership of the read buffer
	if (Memory == nullptr && Buffer.IsMemoryOwned())
	{
		const bool bReleased = Buffer.Release().IsOk();
		check(bReleased);
	}

	Memory = nullptr;
}
	
void FChunkReadFileRequest::WaitCompletionImpl(float TimeLimitSeconds)
{
	WaitForChunkRequest(TimeLimitSeconds);
}

void FChunkReadFileRequest::CancelImpl()
{
	bCanceled = true;
	CancelChunkRequest();
}

void FChunkReadFileRequest::HandleChunkResult(TIoStatusOr<FIoBuffer>&& Result)
{
	bCanceled = Result.Status().IsOk() == false;
	SetDataComplete();
	SetAllComplete();
}

//////////////////////////////////////////////////////////////////////////////

class FChunkFileSizeRequest : public IAsyncReadRequest
{
public:
	FChunkFileSizeRequest(const FIoChunkId& ChunkId, FAsyncFileCallBack* Callback)
		: IAsyncReadRequest(Callback, true, nullptr)
	{
		TIoStatusOr<uint64> Result = FIoDispatcher::Get().GetSizeForChunk(ChunkId);
		if (Result.IsOk())
		{
			Size = Result.ValueOrDie();
		}

		SetComplete();
	}

	virtual ~FChunkFileSizeRequest() = default;

private:

	virtual void WaitCompletionImpl(float TimeLimitSeconds) override
	{
		// Even though SetComplete called in the constructor and sets bCompleteAndCallbackCalled=true, we still need to implement WaitComplete as
		// the CompleteCallback can end up starting async tasks that can overtake the constructor execution and need to wait for the constructor to finish.
		while (!*(volatile bool*)&bCompleteAndCallbackCalled);
	}

	virtual void CancelImpl() override
	{
	}
};

//////////////////////////////////////////////////////////////////////////////

class FChunkReadFileHandle : public IAsyncReadFileHandle
{
public:
	FChunkReadFileHandle(const FIoChunkId& InChunkId) 
		: ChunkId(InChunkId)
	{
	}

	virtual ~FChunkReadFileHandle() = default;

	virtual IAsyncReadRequest* SizeRequest(FAsyncFileCallBack* CompleteCallback = nullptr) override;

	virtual IAsyncReadRequest* ReadRequest(
		int64 Offset,
		int64 BytesToRead,
		EAsyncIOPriorityAndFlags PriorityAndFlags = AIOP_Normal,
		FAsyncFileCallBack* CompleteCallback = nullptr,
		uint8* UserSuppliedMemory = nullptr) override;

private:
	FIoChunkId ChunkId;
};

IAsyncReadRequest* FChunkReadFileHandle::SizeRequest(FAsyncFileCallBack* CompleteCallback)
{
	return new FChunkFileSizeRequest(ChunkId, CompleteCallback);
}

IAsyncReadRequest* FChunkReadFileHandle::ReadRequest(
	int64 Offset, 
	int64 BytesToRead, 
	EAsyncIOPriorityAndFlags PriorityAndFlags,
	FAsyncFileCallBack* CompleteCallback,
	uint8* UserSuppliedMemory)
{
	FIoBuffer Buffer = UserSuppliedMemory ? FIoBuffer(FIoBuffer::Wrap, UserSuppliedMemory, BytesToRead) : FIoBuffer(BytesToRead);
	FChunkReadFileRequest* Request = new FChunkReadFileRequest(CompleteCallback, MoveTemp(Buffer));

	Request->Issue(ChunkId, FIoReadOptions(Offset, BytesToRead), ConvertToIoDispatcherPriority(PriorityAndFlags));

	return Request;
}

//////////////////////////////////////////////////////////////////////////////

class FChunkBulkDataRequest final : public FChunkRequest, public IBulkDataIORequest
{
public:
	FChunkBulkDataRequest(FBulkDataIORequestCallBack* InCallback, FIoBuffer&& InBuffer);
	
	virtual ~FChunkBulkDataRequest() = default;
	
	inline virtual bool PollCompletion() const override
	{
		checkf(GetStatus() != EChunkRequestStatus::None, TEXT("The request must be issued before polling for completion"));
		return EnumHasAnyFlags(GetStatus(), EChunkRequestStatus::CallbackTriggered);
	}

	virtual bool WaitCompletion(float TimeLimitSeconds = 0.0f) override
	{
		checkf(GetStatus() != EChunkRequestStatus::None, TEXT("The request must be issued before waiting for completion"));
		return WaitForChunkRequest(TimeLimitSeconds);
	}

	virtual uint8* GetReadResults() override;
	
	inline virtual int64 GetSize() const override
	{
		checkf(GetStatus() != EChunkRequestStatus::None, TEXT("The request must be issued before polling for size"));
		return EnumHasAnyFlags(GetStatus(), EChunkRequestStatus::DataReady) ? GetSizeResult() : -1;
	}

	virtual void Cancel() override
	{
		CancelChunkRequest();
	}
	
private:

	virtual void HandleChunkResult(TIoStatusOr<FIoBuffer>&& Result) override
	{
		if (Callback)
		{
			const bool bCanceled = Result.IsOk() == false;
			Callback(bCanceled, this);
		}
	}

	FBulkDataIORequestCallBack Callback;
};

FChunkBulkDataRequest::FChunkBulkDataRequest(FBulkDataIORequestCallBack* InCallback, FIoBuffer&& InBuffer)
	: FChunkRequest(MoveTemp(InBuffer))
{
	if (InCallback)
	{
		Callback = *InCallback;
	}
}

uint8* FChunkBulkDataRequest::GetReadResults()
{
	uint8* ReadResult = nullptr;

	if (EnumHasAnyFlags(GetStatus(), EChunkRequestStatus::DataReady))
	{
		if (Buffer.IsMemoryOwned())
		{
			ReadResult = Buffer.Release().ConsumeValueOrDie();
		}
		else
		{
			ReadResult = Buffer.GetData();
		}
	}

	return ReadResult;
}

//////////////////////////////////////////////////////////////////////////////

TUniquePtr<IBulkDataIORequest> CreateBulkDataIoDispatcherRequest(
	const FIoChunkId& ChunkId,
	int64 Offset,
	int64 Size,
	FBulkDataIORequestCallBack* Callback,
	uint8* UserSuppliedMemory,
	int32 Priority)
{
	FIoBuffer Buffer = UserSuppliedMemory ? FIoBuffer(FIoBuffer::Wrap, UserSuppliedMemory, Size) : FIoBuffer(Size);

	TUniquePtr<FChunkBulkDataRequest> Request = MakeUnique<FChunkBulkDataRequest>(Callback, MoveTemp(Buffer));
	Request->Issue(ChunkId, FIoReadOptions(Offset, Size), Priority);

	return Request;
}

//////////////////////////////////////////////////////////////////////////////

bool OpenReadBulkData(
	const FBulkMetaData& BulkMeta,
	const FBulkDataChunkId& BulkChunkId,
	int64 Offset,
	int64 Size,
	EAsyncIOPriorityAndFlags Priority,
	TFunction<void(FArchive& Ar)>&& Read)
{
	if (BulkChunkId.IsValid() == false)
	{
		return false;
	}

	if (FPackageId PackageId = BulkChunkId.GetPackageId(); PackageId.IsValid())
	{
		const FIoChunkId ChunkId = CreateBulkDataIoChunkId(BulkMeta, PackageId);
		FIoBatch Batch = FIoDispatcher::Get().NewBatch();

		FIoRequest Request = Batch.Read(ChunkId, FIoReadOptions(Offset, Size), ConvertToIoDispatcherPriority(Priority));
		FEventRef Event;
		Batch.IssueAndTriggerEvent(Event.Get());
		Event->Wait();

		if (const FIoBuffer* Buffer = Request.GetResult())
		{
			FMemoryReaderView Ar(Buffer->GetView());
			Read(Ar);

			return true;
		}
	}
	else
	{
		IPackageResourceManager& ResourceMgr = IPackageResourceManager::Get();
		
		const bool bExternalResource = BulkMeta.HasAllFlags(static_cast<EBulkDataFlags>(BULKDATA_PayloadInSeperateFile | BULKDATA_WorkspaceDomainPayload));
		const FPackagePath& Path = BulkChunkId.GetPackagePath();

		TUniquePtr<FArchive> Ar;
		if (bExternalResource)
		{
			Ar = ResourceMgr.OpenReadExternalResource(EPackageExternalResource::WorkspaceDomainFile, Path.GetPackageName());
		}
		else
		{
			const EPackageSegment Segment = GetPackageSegmentFromFlags(BulkMeta);
			Ar = ResourceMgr.OpenReadPackage(Path, Segment).Archive;
		}

		if (Ar)
		{
			Ar->Seek(Offset);
			Read(*Ar);

			return true;
		}
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////////

TUniquePtr<IAsyncReadFileHandle> OpenAsyncReadBulkData(const FBulkMetaData& BulkMeta, const FBulkDataChunkId& BulkChunkId)
{
	if (BulkChunkId.IsValid() == false)
	{
		return TUniquePtr<IAsyncReadFileHandle>();
	}

	if (FPackageId PackageId = BulkChunkId.GetPackageId(); PackageId.IsValid())
	{
		return MakeUnique<FChunkReadFileHandle>(CreateBulkDataIoChunkId(BulkMeta, PackageId));
	}
	else
	{
		IPackageResourceManager& ResourceMgr = IPackageResourceManager::Get();
		
		const bool bExternalResource = BulkMeta.HasAllFlags(static_cast<EBulkDataFlags>(BULKDATA_PayloadInSeperateFile | BULKDATA_WorkspaceDomainPayload));
		const FPackagePath& Path = BulkChunkId.GetPackagePath();

		if (bExternalResource)
		{
			return ResourceMgr.OpenAsyncReadExternalResource(EPackageExternalResource::WorkspaceDomainFile, Path.GetPackageName()).Handle;
		}
		else
		{
			const EPackageSegment Segment = GetPackageSegmentFromFlags(BulkMeta);
			return ResourceMgr.OpenAsyncReadPackage(Path, Segment).Handle;
		}
	}
}

//////////////////////////////////////////////////////////////////////////////

TUniquePtr<IBulkDataIORequest> CreateStreamingRequest(
	const FBulkMetaData& BulkMeta,
	const FBulkDataChunkId& BulkChunkId,
	int64 Offset,
	int64 Size,
	EAsyncIOPriorityAndFlags Priority,
	FBulkDataIORequestCallBack* Callback,
	uint8* UserSuppliedMemory)
{
	if (BulkChunkId.IsValid() == false)
	{
		return TUniquePtr<IBulkDataIORequest>();
	}
	
	if (FPackageId PackageId = BulkChunkId.GetPackageId(); PackageId.IsValid())
	{
		FIoBuffer Buffer = UserSuppliedMemory ? FIoBuffer(FIoBuffer::Wrap, UserSuppliedMemory, Size) : FIoBuffer(Size);
		const FIoChunkId ChunkId = CreateBulkDataIoChunkId(BulkMeta, PackageId);

		FChunkBulkDataRequest* Request = new FChunkBulkDataRequest(Callback, MoveTemp(Buffer));
		Request->Issue(ChunkId, FIoReadOptions(Offset, Size), ConvertToIoDispatcherPriority(Priority));
		
		return TUniquePtr<IBulkDataIORequest>(Request);
	}
	else if (TUniquePtr<IAsyncReadFileHandle> FileHandle = OpenAsyncReadBulkData(BulkMeta, BulkChunkId); FileHandle.IsValid())
	{
		TUniquePtr<FBulkDataIORequest> Request = MakeUnique<FBulkDataIORequest>(FileHandle.Release());

		if (Request->MakeReadRequest(Offset, Size, Priority, Callback, UserSuppliedMemory))
		{
			return Request;
		}
	}

	return TUniquePtr<IBulkDataIORequest>();
}

//////////////////////////////////////////////////////////////////////////////

bool DoesBulkDataExist(const FBulkMetaData& BulkMeta, const FBulkDataChunkId& BulkChunkId)
{
	if (BulkChunkId.IsValid() == false)
	{
		return false;
	}

	if (FPackageId Id = BulkChunkId.GetPackageId(); Id.IsValid())
	{
		const FIoChunkId ChunkId = CreateBulkDataIoChunkId(BulkMeta, Id);
		return FIoDispatcher::Get().DoesChunkExist(ChunkId);
	}
	else
	{
		IPackageResourceManager& ResourceMgr = IPackageResourceManager::Get();
		const FPackagePath& PackagePath = BulkChunkId.GetPackagePath();
		const bool bExternalResource = BulkMeta.HasAllFlags(static_cast<EBulkDataFlags>(BULKDATA_PayloadInSeperateFile | BULKDATA_WorkspaceDomainPayload));

		if (bExternalResource)
		{
			return ResourceMgr.DoesExternalResourceExist(
				EPackageExternalResource::WorkspaceDomainFile, PackagePath.GetPackageName());
		}

		const EPackageSegment PackageSegment = GetPackageSegmentFromFlags(BulkMeta);
		return ResourceMgr.DoesPackageExist(PackagePath, PackageSegment);
	}
}

//////////////////////////////////////////////////////////////////////////////

bool TryMemoryMapBulkData(
	const FBulkMetaData& BulkMeta,
	const FBulkDataChunkId& BulkChunkId,
	int64 Offset,
	int64 Size,
	FIoMappedRegion& OutRegion)
{
	if (FPackageId Id = BulkChunkId.GetPackageId(); Id.IsValid())
	{
		const FIoChunkId ChunkId = CreateBulkDataIoChunkId(BulkMeta, Id);
		TIoStatusOr<FIoMappedRegion> Status = FIoDispatcher::Get().OpenMapped(ChunkId, FIoReadOptions(Offset, Size));

		if (Status.IsOk())
		{
			OutRegion = Status.ConsumeValueOrDie();

			return true;
		}
	}
	else
	{
		IPackageResourceManager& ResourceMgr = IPackageResourceManager::Get();
		const FPackagePath& Path = BulkChunkId.GetPackagePath();
		const EPackageSegment Segment = EPackageSegment::BulkDataMemoryMapped;

		TUniquePtr<IMappedFileHandle> MappedFile;
		MappedFile.Reset(IPackageResourceManager::Get().OpenMappedHandleToPackage(Path, Segment));

		if (!MappedFile)
		{
			return false;
		}

		if (IMappedFileRegion* MappedRegion = MappedFile->MapRegion(Offset, Size, true))
		{
			OutRegion.MappedFileHandle = MappedFile.Release();
			OutRegion.MappedFileRegion = MappedRegion;

			return true;
		}
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////////

class FAsyncBulkDataRequests
{
public:
	void AddPendingRequest(
		FBulkData* Owner,
		TUniquePtr<IAsyncReadFileHandle>&& FileHandle,
		TUniquePtr<IAsyncReadRequest>&& ReadRequest)
	{
		FScopeLock _(&RequestsCS);
		PendingRequests.Add(Owner, FPendingRequest { MoveTemp(FileHandle), MoveTemp(ReadRequest) });
	}

	void Flush(FBulkData* Owner)
	{
		FPendingRequest PendingRequest;

		{
			FScopeLock _(&RequestsCS);
			PendingRequest = MoveTemp(PendingRequests.FindChecked(Owner));
			PendingRequests.Remove(Owner);
		}
		
		PendingRequest.ReadRequest->WaitCompletion();
	}

	static FAsyncBulkDataRequests& Get()
	{
		static FAsyncBulkDataRequests Instance;
		return Instance;
	}

private:
	struct FPendingRequest
	{
		TUniquePtr<IAsyncReadFileHandle> FileHandle;
		TUniquePtr<IAsyncReadRequest> ReadRequest;
	};

	TMap<FBulkData*, FPendingRequest> PendingRequests;
	FCriticalSection RequestsCS;
};

bool StartAsyncLoad(
	FBulkData* Owner,
	const FBulkMetaData& BulkMeta,
	const FBulkDataChunkId& BulkChunkId,
	int64 Offset,
	int64 Size,
	EAsyncIOPriorityAndFlags Priority,
	TFunction<void(TIoStatusOr<FIoBuffer>)>&& Callback)
{
	if (TUniquePtr<IAsyncReadFileHandle> FileHandle = OpenAsyncReadBulkData(BulkMeta, BulkChunkId))
	{
		FAsyncFileCallBack FileReadCallback = [Callback = MoveTemp(Callback), Size = Size]
			(bool bCanceled, IAsyncReadRequest* Request)
			{
				if (bCanceled)
				{
					Callback(FIoStatus(EIoErrorCode::Cancelled));
				}
				else if (uint8* Data = Request->GetReadResults())
				{
					Callback(FIoBuffer(FIoBuffer::AssumeOwnership, Data, Size));
				}
				else
				{
					Callback(FIoStatus(EIoErrorCode::ReadError));
				}
			};
		
		if (IAsyncReadRequest* Request = FileHandle->ReadRequest(Offset, Size, Priority, &FileReadCallback))
		{
			FAsyncBulkDataRequests::Get().AddPendingRequest(Owner, MoveTemp(FileHandle), TUniquePtr<IAsyncReadRequest>(Request));
			return true;
		}
	}

	return false;
}

void FlushAsyncLoad(FBulkData* Owner)
{
	FAsyncBulkDataRequests::Get().Flush(Owner);
}

} // namespace UE::BulkData

//////////////////////////////////////////////////////////////////////////////

class FBulkDataRequest::FBase
{
public:
	virtual ~FBase() = default;

	virtual void Cancel() = 0;

	virtual bool Issue(TArrayView<FReadRequest> Requests, EAsyncIOPriorityAndFlags Priority) = 0;

	bool Wait(uint32 Milliseconds)
	{
		return DoneEvent.Wait(Milliseconds);
	}

	inline FBulkDataRequest::EStatus GetStatus() const
	{
		return static_cast<FBulkDataRequest::EStatus>(Status.load(std::memory_order_consume));
	}

protected:

	FBase(FCompletionCallback&& Callback)
		: DoneEvent(EEventMode::ManualReset)
		, CompletionCallback(MoveTemp(Callback))
	{
	}

	inline void SetStatus(FBulkDataRequest::EStatus InStatus)
	{
		Status.store(uint32(InStatus), std::memory_order_release); 
	}

	inline void CompleteRequest(FBulkDataRequest::EStatus CompletionStatus)
	{
		SetStatus(CompletionStatus);

		if (CompletionCallback)
		{
			CompletionCallback(CompletionStatus);
		}

		DoneEvent.Trigger();
	}

	std::atomic<uint32>	Status;
	UE::FLazyEvent DoneEvent;
	FCompletionCallback CompletionCallback;
};

//////////////////////////////////////////////////////////////////////////////

class FBulkDataRequest::FReadChunk final : public FBulkDataRequest::FBase
{
public:
	FReadChunk(FCompletionCallback&& Callback)
		: FBase(MoveTemp(Callback))
	{
	}

	virtual ~FReadChunk()
	{ 
		Cancel();
		Wait(MAX_uint32);
	}

	virtual void Cancel() override
	{
		if (GetStatus() == FBulkDataRequest::EStatus::Pending)
		{
			for (FIoRequest& Request : PendingRequests)
			{
				Request.Cancel();
			}
		}
	}

	virtual bool Issue(TArrayView<FReadRequest> Requests, EAsyncIOPriorityAndFlags Priority) override
	{
		check(Requests.Num() > 0);
		
		PendingRequests.Reserve(Requests.Num());

		SetStatus(EStatus::Pending);

		FIoBatch Batch = FIoDispatcher::Get().NewBatch();
		const int32 IoDispatcherPriority = ConvertToIoDispatcherPriority(Priority);

		for (FReadRequest& Request : Requests)
		{
			const FIoChunkId ChunkId = CreateBulkDataIoChunkId(Request.BulkMeta, Request.BulkChunkId.GetPackageId());
			PendingRequests.Add(Batch.Read(ChunkId, FIoReadOptions(Request.Offset, Request.Size, Request.TargetVa), IoDispatcherPriority));
		}

		Batch.IssueWithCallback([this]()
		{
			FBulkDataRequest::EStatus BatchStatus = FBulkDataRequest::EStatus::Ok;

			for (const FIoRequest& Request : PendingRequests)
			{
				if (EIoErrorCode ErrorCode = Request.Status().GetErrorCode(); ErrorCode != EIoErrorCode::Ok)
				{
					BatchStatus = ErrorCode == EIoErrorCode::Cancelled
						? FBulkDataRequest::EStatus::Cancelled
						: FBulkDataRequest::EStatus::Error;

					break;
				}
			}
			
			PendingRequests.Empty();
			CompleteRequest(BatchStatus);
		});

		return true;
	}

private:

	TArray<FIoRequest, TInlineAllocator<4>> PendingRequests;
};

//////////////////////////////////////////////////////////////////////////////

class FBulkDataRequest::FReadFile final : public FBulkDataRequest::FBase
{
public:
	FReadFile(FCompletionCallback&& Callback)
		: FBase(MoveTemp(Callback))
	{
	}

	virtual ~FReadFile()
	{ 
		Cancel();
		Wait(MAX_uint32);
	}

	virtual void Cancel() override
	{
		if (GetStatus() == FBulkDataRequest::EStatus::Pending)
		{
			for (TUniquePtr<IAsyncReadRequest>& Request : PendingRequests)
			{
				Request->Cancel();
			}
		}
	}

	virtual bool Issue(TArrayView<FReadRequest> Requests, EAsyncIOPriorityAndFlags Priority) override
	{
		using namespace UE::BulkData::Private;

		check(Requests.Num() > 0);
		
		SetStatus(EStatus::Pending);
		
		PendingRequestCount = Requests.Num();
		PendingStatus = FBulkDataRequest::EStatus::Ok;
		
		for (FReadRequest& Request : Requests)
		{
			IAsyncReadFileHandle* FileHandle = OpenFile(Request.BulkMeta, Request.BulkChunkId);

			if (FileHandle == nullptr)
			{
				return false;
			}

			FAsyncFileCallBack ReadFileCallback = [this](bool bWasCancelled, IAsyncReadRequest* ReadRequest)
			{
				if (bWasCancelled)
				{
					PendingStatus = FBulkDataRequest::EStatus::Cancelled;
				}

				if (1 == PendingRequestCount.fetch_sub(1))
				{
					CompleteRequest(PendingStatus);
				}
			};

			if (ReadFile(*FileHandle, Request.Offset, Request.Size, Priority, &ReadFileCallback, Request.TargetVa) == false)
			{
				SetStatus(EStatus::Error);

				return false;
			}
		}

		return true;
	}

private:

	IAsyncReadFileHandle* OpenFile(const UE::BulkData::Private::FBulkMetaData& BulkMeta, const UE::BulkData::Private::FBulkDataChunkId& BulkChunkId)
	{
		const FPackagePath Path = BulkChunkId.GetPackagePath();

		TUniquePtr<IAsyncReadFileHandle>& FileHandle = PathToFileHandle.FindOrAdd(Path.GetPackageFName());

		if (FileHandle.IsValid() == false)
		{
			IPackageResourceManager& ResourceMgr = IPackageResourceManager::Get();

			const bool bExternalResource = BulkMeta.HasAllFlags(static_cast<EBulkDataFlags>(BULKDATA_PayloadInSeperateFile | BULKDATA_WorkspaceDomainPayload));

			if (bExternalResource)
			{
				FileHandle = ResourceMgr.OpenAsyncReadExternalResource(EPackageExternalResource::WorkspaceDomainFile, Path.GetPackageName()).Handle;
			}
			else
			{
				const EPackageSegment Segment = GetPackageSegmentFromFlags(BulkMeta);
				FileHandle = ResourceMgr.OpenAsyncReadPackage(Path, Segment).Handle;
			}
		}

		return FileHandle.Get();
	}

	bool ReadFile(IAsyncReadFileHandle& FileHandle, int64 Offset, int64 Size, EAsyncIOPriorityAndFlags Priority, FAsyncFileCallBack* Callback, void* Dst)
	{
		TUniquePtr<IAsyncReadRequest> ReadRequest(FileHandle.ReadRequest(Offset, Size, Priority, Callback, reinterpret_cast<uint8*>(Dst)));

		if (ReadRequest.IsValid())
		{
			PendingRequests.Add(MoveTemp(ReadRequest));

			return true;
		}

		return false;
	}

	using FPathToFileHandle = TMap<FName, TUniquePtr<IAsyncReadFileHandle>>;
	using FReadRequests = TArray<TUniquePtr<IAsyncReadRequest>>;
	
	FPathToFileHandle PathToFileHandle;
	FReadRequests PendingRequests;
	FBulkDataRequest::EStatus PendingStatus;
	std::atomic<int32> PendingRequestCount;
};

//////////////////////////////////////////////////////////////////////////////

void FBulkDataRequest::FDeleter::operator()(FBase* InImpl)
{
	delete InImpl;
}

FBulkDataRequest::FBulkDataRequest(FBulkDataRequest::FBasePtr&& InImpl)
	: Impl(MoveTemp(InImpl))
{
}

FBulkDataRequest::EStatus FBulkDataRequest::GetStatus() const
{
	return Impl.IsValid() ? Impl->GetStatus() : EStatus::None;
}

bool FBulkDataRequest::IsNone() const
{
	return FBulkDataRequest::EStatus::None == GetStatus();
}

bool FBulkDataRequest::IsPending() const
{
	return FBulkDataRequest::EStatus::Pending == GetStatus();
}

bool FBulkDataRequest::IsOk() const
{
	return FBulkDataRequest::EStatus::Ok == GetStatus();
}

bool FBulkDataRequest::IsCompleted() const
{
	const uint32 Status = static_cast<uint32>(GetStatus());
	return Status > static_cast<uint32>(EStatus::Pending);
}

void FBulkDataRequest::Wait()
{
	check(Impl);
	Impl->Wait(MAX_uint32);
}

bool FBulkDataRequest::WaitFor(uint32 Milliseconds)
{
	check(Impl);
	return Impl->Wait(Milliseconds);
}

bool FBulkDataRequest::WaitFor(const FTimespan& WaitTime)
{
	return WaitFor((uint32)FMath::Clamp<int64>(WaitTime.GetTicks() / ETimespan::TicksPerMillisecond, 0, MAX_uint32));
}

void FBulkDataRequest::Cancel()
{
	check(Impl);
	Impl->Cancel();
}

void FBulkDataRequest::FRequestBuilder::AddRequest(
	const UE::BulkData::Private::FBulkMetaData& BulkMeta,
	const UE::BulkData::Private::FBulkDataChunkId& BulkChunkId,
	uint64 Offset,
	uint64 Size,
	void* TargetVa)
{
	FReadRequest& Request = Requests.AddDefaulted_GetRef();

	Request.BulkMeta	= BulkMeta;
	Request.BulkChunkId = BulkChunkId;
	Request.Offset		= Offset;
	Request.Size		= Size;
	Request.TargetVa	= TargetVa;

	TotalRequestSize += Size;
}

FBulkDataRequest::FScatterGather& FBulkDataRequest::FScatterGather::Read(const FBulkData& BulkData, uint64 Offset, uint64 Size)
{
	const uint64 BulkOffset	= uint64(BulkData.GetBulkDataOffsetInFile());
	const uint64 BulkSize	= uint64(BulkData.GetBulkDataSize());

	AddRequest(BulkData.BulkMeta, BulkData.BulkChunkId, BulkOffset + Offset, FMath::Min(BulkSize, Size), nullptr);

	return *this;
}

FBulkDataRequest FBulkDataRequest::FScatterGather::Issue(FIoBuffer& Dst, FBulkDataRequest::FCompletionCallback&& Callback, EAsyncIOPriorityAndFlags Priority)
{
	if (Requests.IsEmpty())
	{
		return FBulkDataRequest();
	}

	check(TotalRequestSize > 0);

	Dst = FIoBuffer(TotalRequestSize);
	
	FMutableMemoryView DstView = Dst.GetMutableView();
	for (FReadRequest& Request : Requests)
	{
		Request.TargetVa = DstView.GetData();
		DstView.RightChopInline(Request.Size);
	}

	return FBulkDataRequest::Issue(Requests, Priority, MoveTemp(Callback));
}

FBulkDataRequest::FStreamToInstance& FBulkDataRequest::FStreamToInstance::Read(FBulkData& BulkData)
{
	const uint64 BulkOffset	= uint64(BulkData.GetBulkDataOffsetInFile());
	const uint64 BulkSize	= uint64(BulkData.GetBulkDataSize());
	
	AddRequest(BulkData.BulkMeta, BulkData.BulkChunkId, BulkOffset, BulkSize, nullptr);
	Instances.Add(&BulkData);

	return *this;
}

FBulkDataRequest FBulkDataRequest::FStreamToInstance::Issue(EAsyncIOPriorityAndFlags Priority)
{
	if (Requests.IsEmpty())
	{
		return FBulkDataRequest();
	}

	check(TotalRequestSize > 0);

	FIoBuffer Dst = FIoBuffer(TotalRequestSize);
	FMutableMemoryView DstView = Dst.GetMutableView();

	for (FReadRequest& Request : Requests)
	{
		Request.TargetVa = DstView.GetData();
		DstView.RightChopInline(Request.Size);
	}

	return FBulkDataRequest::Issue(
		Requests,
		Priority,
		[Instances = MoveTemp(Instances), Dst = MoveTemp(Dst)](FBulkDataRequest::EStatus Status)
		{
			if (FBulkDataRequest::EStatus::Ok != Status)
			{
				return;
			}

			FMemoryReaderView Ar(Dst.GetView(), true);
			for (FBulkData* BulkData : Instances)
			{
				const int64 BulkOffset	= BulkData->GetBulkDataOffsetInFile();
				const int64 BulkSize	= BulkData->GetBulkDataSize();
				void* Data				= BulkData->ReallocateData(BulkSize);

				Ar.Seek(BulkOffset);
				BulkData->SerializeBulkData(Ar, Data, BulkSize, EBulkDataFlags(BulkData->GetBulkDataFlags()));
			}
		});
}

FBulkDataRequest::FScatterGather FBulkDataRequest::ScatterGather()
{
	return FBulkDataRequest::FScatterGather();
}

FBulkDataRequest::FStreamToInstance FBulkDataRequest::StreamToInstance()
{
	return FBulkDataRequest::FStreamToInstance();
}

FBulkDataRequest FBulkDataRequest::Issue(TArrayView<FReadRequest> Requests, EAsyncIOPriorityAndFlags Priority, FBulkDataRequest::FCompletionCallback&& Callback)
{
	if (Requests.IsEmpty())
	{
		return FBulkDataRequest();
	}

	check(Algo::AllOf(Requests, [](const FReadRequest& Req)
	{ 
		return Req.BulkChunkId.IsValid() && Req.TargetVa != nullptr;
	}));

	FBasePtr NewRequest;

	if (FPackageId PackageId = Requests[0].BulkChunkId.GetPackageId(); PackageId.IsValid())
	{
		NewRequest.Reset(new FBulkDataRequest::FReadChunk(MoveTemp(Callback)));
	}
	else
	{
		NewRequest.Reset(new FBulkDataRequest::FReadFile(MoveTemp(Callback)));
	}

	if (NewRequest->Issue(Requests, Priority) == false)
	{
		return FBulkDataRequest();
	}

	return FBulkDataRequest(MoveTemp(NewRequest));
}
