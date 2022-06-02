// Copyright Epic Games, Inc. All Rights Reserved.
#include "Async/MappedFileHandle.h"
#include "Experimental/Async/LazyEvent.h"
#include "HAL/CriticalSection.h"
#include "Serialization/BulkData.h"
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
	None		= 0,
	Ok			= 1 << 0,
	Pending		= 1 << 1,
	Canceled	= 1 << 2,
};

class FChunkRequest
{
public:
	virtual ~FChunkRequest();
	
	void Issue(FIoChunkId ChunkId, FIoReadOptions Options, int32 Priority);

	inline EChunkRequestStatus GetStatus() const
	{
		return static_cast<EChunkRequestStatus>(Status.load(std::memory_order_relaxed));
	}

protected:
	FChunkRequest(FIoBuffer&& InBuffer);

	virtual void HandleChunkResult(TIoStatusOr<FIoBuffer>&& Result) = 0;
	bool WaitForChunkRequest(float TimeLimitSeconds = 0.0f);
	void CancelChunkRequest();
	
	FIoBuffer			Buffer;

private:

	UE::FLazyEvent		DoneEvent;
	FIoRequest			Request;
	std::atomic<uint32>	Status;
};

FChunkRequest::FChunkRequest(FIoBuffer&& InBuffer)
	: Buffer(MoveTemp(InBuffer))
	, DoneEvent(EEventMode ::ManualReset)
	, Status{0}
{
}

FChunkRequest::~FChunkRequest()
{
	DoneEvent.Wait();
}

void FChunkRequest::Issue(FIoChunkId ChunkId, FIoReadOptions Options, int32 Priority)
{
	Status.store(uint32(EChunkRequestStatus::Pending), std::memory_order_relaxed); 

	check(Options.GetSize() == Buffer.GetSize());
	Options.SetTargetVa(Buffer.GetData());

	FIoBatch IoBatch = FIoDispatcher::Get().NewBatch();
	Request = IoBatch.ReadWithCallback(ChunkId, Options, Priority, [this](TIoStatusOr<FIoBuffer> Result)
	{
		Status.store(uint32(Result.IsOk() ? EChunkRequestStatus::Ok : EChunkRequestStatus::Canceled), std::memory_order_relaxed); 
		HandleChunkResult(MoveTemp(Result));
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
		return GetStatus() != EChunkRequestStatus::Pending;
	}

	virtual bool WaitCompletion(float TimeLimitSeconds = 0.0f) override
	{
		checkf(GetStatus() != EChunkRequestStatus::None, TEXT("The request must be issued before waiting for completion"));
		return WaitForChunkRequest(TimeLimitSeconds);
	}

	virtual uint8* GetReadResults() override;
	
	inline virtual int64 GetSize() const override
	{
		return GetStatus() == EChunkRequestStatus::Ok ? Buffer.GetSize() : -1;
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

	if (GetStatus() == EChunkRequestStatus::Ok)
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

		FIoRequest Request = Batch.Read(ChunkId, FIoReadOptions(Offset, Size), IoDispatcherPriority_Medium);
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
		Request->Issue(ChunkId, FIoReadOptions(Offset, Size), Priority);
		
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
		
		if (IAsyncReadRequest* Request = FileHandle->ReadRequest(Offset, Size, AIOP_Normal, &FileReadCallback))
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
