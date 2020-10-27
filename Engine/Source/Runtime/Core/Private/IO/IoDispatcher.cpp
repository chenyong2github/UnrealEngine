// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/IoDispatcher.h"
#include "IO/IoDispatcherPrivate.h"
#include "IO/IoStore.h"
#include "IO/IoDispatcherFileBackend.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/CoreDelegates.h"
#include "Math/RandomStream.h"
#include "Misc/ScopeLock.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformProcess.h"
#include "Serialization/LargeMemoryReader.h"
#include "GenericPlatform/GenericPlatformChunkInstall.h"
#include "HAL/Event.h"
#include "Async/MappedFileHandle.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"

DEFINE_LOG_CATEGORY(LogIoDispatcher);


#if CSV_PROFILER && IS_MONOLITHIC
extern int64 GTotalLoaded;
#endif



const FIoChunkId FIoChunkId::InvalidChunkId = FIoChunkId::CreateEmptyId();

TUniquePtr<FIoDispatcher> GIoDispatcher;

#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
//PRAGMA_DISABLE_OPTIMIZATION
#endif

TRACE_DECLARE_INT_COUNTER(PendingIoRequests, TEXT("IoDispatcher/PendingIoRequests"));

/** A utility function to convert a EIoStoreResolveResult to the corresponding FIoStatus.*/
FIoStatus ToStatus(EIoStoreResolveResult Result)
{
	switch (Result)
	{
	case IoStoreResolveResult_OK:
		return FIoStatus(EIoErrorCode::Ok);

	case IoStoreResolveResult_NotFound:
		return FIoStatus(EIoErrorCode::NotFound);

	default:
		return FIoStatus(EIoErrorCode::Unknown);
	}
}

template <typename T, uint32 BlockSize = 128>
class TBlockAllocator
{
public:
	~TBlockAllocator()
	{
		FreeBlocks();
	}

	FORCEINLINE T* Alloc()
	{
		FScopeLock _(&CriticalSection);

		if (!NextFree)
		{
			//TODO: Virtual alloc
			FBlock* Block = new FBlock;

			for (int32 ElementIndex = 0; ElementIndex < BlockSize; ++ElementIndex)
			{
				FElement* Element = &Block->Elements[ElementIndex];
				Element->Next = NextFree;
				NextFree = Element;
			}

			Block->Next = Blocks;
			Blocks = Block;
		}

		FElement* Element = NextFree;
		NextFree = Element->Next;

		++NumElements;

		return Element->Buffer.GetTypedPtr();
	}

	FORCEINLINE void Free(T* Ptr)
	{
		FScopeLock _(&CriticalSection);

		FElement* Element = reinterpret_cast<FElement*>(Ptr);
		Element->Next = NextFree;
		NextFree = Element;

		--NumElements;
	}

	template <typename... ArgsType>
	T* Construct(ArgsType&&... Args)
	{
		return new(Alloc()) T(Forward<ArgsType>(Args)...);
	}

	void Destroy(T* Ptr)
	{
		Ptr->~T();
		Free(Ptr);
	}

	void Trim()
	{
		FScopeLock _(&CriticalSection);
		if (!NumElements)
		{
			FreeBlocks();
		}
	}

private:
	void FreeBlocks()
	{
		FBlock* Block = Blocks;
		while (Block)
		{
			FBlock* Tmp = Block;
			Block = Block->Next;
			delete Tmp;
		}

		Blocks = nullptr;
		NextFree = nullptr;
		NumElements = 0;
	}

	struct FElement
	{
		TTypeCompatibleBytes<T> Buffer;
		FElement* Next;
	};

	struct FBlock
	{
		FElement Elements[BlockSize];
		FBlock* Next = nullptr;
	};

	FBlock*				Blocks = nullptr;
	FElement*			NextFree = nullptr;
	int32				NumElements = 0;
	FCriticalSection	CriticalSection;
};

class FIoDispatcherImpl
	: public FRunnable
{
public:
	FIoDispatcherImpl(bool bInIsMultithreaded)
		: bIsMultithreaded(bInIsMultithreaded)
		, FileIoStore(EventQueue, SignatureErrorEvent, bIsMultithreaded) 
	{
		FCoreDelegates::GetMemoryTrimDelegate().AddLambda([this]()
		{
			RequestAllocator.Trim();
			BatchAllocator.Trim();
		});
	}

	~FIoDispatcherImpl()
	{
		delete Thread;
	}

	FIoStatus Initialize()
	{
		return FIoStatus::Ok;
	}

	bool InitializePostSettings()
	{
		FileIoStore.Initialize();
		Thread = FRunnableThread::Create(this, TEXT("IoDispatcher"), 0, TPri_AboveNormal, FPlatformAffinity::GetIoDispatcherThreadMask());
		return true;
	}

	FIoRequestImpl* AllocRequest(const FIoChunkId& ChunkId, FIoReadOptions Options)
	{
		LLM_SCOPE(ELLMTag::FileSystem);
		FIoRequestImpl* Request = RequestAllocator.Construct();

		Request->ChunkId = ChunkId;
		Request->Options = Options;
		Request->Status = FIoStatus::Unknown;

		return Request;
	}

	FIoRequestImpl* AllocRequest(FIoBatchImpl* Batch, const FIoChunkId& ChunkId, FIoReadOptions Options)
	{
		FIoRequestImpl* Request = AllocRequest(ChunkId, Options);

		Request->Batch = Batch;

		if (Batch->HeadRequest == nullptr)
		{
			Batch->HeadRequest = Request;
			Batch->TailRequest = Request;
		}
		else
		{
			Batch->TailRequest->BatchNextRequest = Request;
			Batch->TailRequest = Request;
		}

		check(Batch->TailRequest->BatchNextRequest == nullptr);
		++Batch->UnfinishedRequestsCount;

		return Request;
	}

	void FreeRequest(FIoRequestImpl* Request)
	{
		RequestAllocator.Destroy(Request);
	}

	FIoBatchImpl* AllocBatch()
	{
		LLM_SCOPE(ELLMTag::FileSystem);
		FIoBatchImpl* Batch = BatchAllocator.Construct();

		return Batch;
	}

	void FreeBatch(FIoBatchImpl* Batch)
	{
		if (Batch != nullptr)
		{
			FIoRequestImpl* Request = Batch->HeadRequest;

			while (Request)
			{
				FIoRequestImpl* Tmp = Request;
				Request = Request->BatchNextRequest;
				FreeRequest(Tmp);
			}

			BatchAllocator.Destroy(Batch);
		}
	}

	void OnNewWaitingRequestsAdded()
	{
		if (bIsMultithreaded)
		{
			EventQueue.DispatcherNotify();
		}
		else
		{
			ProcessIncomingRequests();
			while (PendingIoRequestsCount > 0)
			{
				ProcessCompletedRequests();
			}
		}
	}

	void ReadWithCallback(const FIoChunkId& ChunkId, const FIoReadOptions& Options, EIoDispatcherPriority Priority, FIoReadCallback&& Callback)
	{
		FIoRequestImpl* Request = AllocRequest(ChunkId, Options);
		Request->Callback = MoveTemp(Callback);
		Request->Priority = Priority;
		Request->NextRequest = nullptr;
		{
			FScopeLock _(&WaitingLock);
			if (!WaitingRequestsTail)
			{
				WaitingRequestsHead = WaitingRequestsTail = Request;
			}
			else
			{
				WaitingRequestsTail->NextRequest = Request;
				WaitingRequestsTail = Request;
			}
		}
		OnNewWaitingRequestsAdded();
	}

	TIoStatusOr<FIoMappedRegion> OpenMapped(const FIoChunkId& ChunkId, const FIoReadOptions& Options)
	{
		if (ChunkId.IsValid())
		{
			return FileIoStore.OpenMapped(ChunkId, Options);
		}
		else
		{
			return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("FIoChunkId is not valid"));
		}
	}

	FIoStatus Mount(const FIoStoreEnvironment& Environment, const FGuid& EncryptionKeyGuid, const FAES::FAESKey& EncryptionKey)
	{
		TIoStatusOr<FIoContainerId> ContainerId = FileIoStore.Mount(Environment, EncryptionKeyGuid, EncryptionKey);

		if (ContainerId.IsOk())
		{
			FIoDispatcherMountedContainer MountedContainer;
			MountedContainer.ContainerId = ContainerId.ValueOrDie();
			MountedContainer.Environment = Environment;
			if (ContainerMountedEvent.IsBound())
			{
				ContainerMountedEvent.Broadcast(MountedContainer);
			}
			FScopeLock Lock(&MountedContainersCritical);
			MountedContainers.Add(MoveTemp(MountedContainer));
			return FIoStatus::Ok;
		}

		return ContainerId.Status();
	}

	bool DoesChunkExist(const FIoChunkId& ChunkId) const
	{
		return FileIoStore.DoesChunkExist(ChunkId);
	}

	TIoStatusOr<uint64> GetSizeForChunk(const FIoChunkId& ChunkId) const
	{
		// Only attempt to find the size if the FIoChunkId is valid
		if (ChunkId.IsValid())
		{
			return FileIoStore.GetSizeForChunk(ChunkId);
		}
		else
		{
			return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("FIoChunkId is not valid"));
		}	
	}

	TArray<FIoDispatcherMountedContainer> GetMountedContainers() const
	{
		FScopeLock Lock(&MountedContainersCritical);
		return MountedContainers;
	}

	FIoDispatcher::FIoContainerMountedEvent& OnContainerMounted()
	{
		return ContainerMountedEvent;
	}

	FIoSignatureErrorEvent& GetSignatureErrorEvent()
	{
		return SignatureErrorEvent;
	}

	template<typename Func>
	void IterateBatch(const FIoBatchImpl* Batch, Func&& InCallbackFunction)
	{
		FIoRequestImpl* Request = Batch->HeadRequest;

		while (Request)
		{
			const bool bDoContinue = InCallbackFunction(Request);

			Request = bDoContinue ? Request->BatchNextRequest : nullptr;
		}
	}

	void IssueBatch(const FIoBatchImpl* Batch, EIoDispatcherPriority Priority)
	{
		{
			FScopeLock _(&WaitingLock);
			if (!WaitingRequestsHead)
			{
				WaitingRequestsHead = Batch->HeadRequest;
			}
			else
			{
				WaitingRequestsTail->NextRequest = Batch->HeadRequest;
			}
			WaitingRequestsTail = Batch->TailRequest;
			FIoRequestImpl* Request = Batch->HeadRequest;
			while (Request)
			{
				Request->NextRequest = Request->BatchNextRequest;
				Request->Priority = Priority;
				Request = Request->BatchNextRequest;
			}
		}
		OnNewWaitingRequestsAdded();
	}

	FIoStatus SetupBatchForContiguousRead(FIoBatchImpl* Batch, void* InTargetVa, FIoReadCallback&& InCallback)
	{
		// Create the buffer
		uint64 TotalSize = 0;
		for (FIoRequestImpl* Request = Batch->HeadRequest; Request; Request = Request->BatchNextRequest)
		{
			TIoStatusOr<uint64> SizeResult = GetSizeForChunk(Request->ChunkId);
			if (SizeResult.IsOk())
			{
				TotalSize += FMath::Min(SizeResult.ConsumeValueOrDie(), Request->Options.GetSize());
			}
		}

		// Set up memory buffers
		if (InTargetVa != nullptr)
		{
			Batch->IoBuffer = FIoBuffer(FIoBuffer::Wrap, InTargetVa, TotalSize);
		}
		else
		{
			Batch->IoBuffer = FIoBuffer(FIoBuffer::AssumeOwnership, FMemory::Malloc(TotalSize), TotalSize);
		}

		uint8* DstBuffer = Batch->IoBuffer.Data();

		// Now assign to each request
		uint8* Ptr = DstBuffer;
		for (FIoRequestImpl* Request = Batch->HeadRequest; Request; Request = Request->BatchNextRequest)
		{
			if (Request->Options.GetTargetVa() != nullptr)
			{
				return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("A FIoBatch reading to a contiguous buffer cannot contain FIoRequests that have a TargetVa"));
			}

			Request->Options.SetTargetVa(Ptr);

			TIoStatusOr<uint64> SizeResult = GetSizeForChunk(Request->ChunkId);
			if (SizeResult.IsOk())
			{
				Ptr += FMath::Min(SizeResult.ConsumeValueOrDie(), Request->Options.GetSize());
			}
		}

		// Set up callback
		Batch->Callback = MoveTemp(InCallback);

		return FIoStatus(EIoErrorCode::Ok);
	}

private:
	friend class FIoBatch;

	void ProcessCompletedRequests()
	{
		//TRACE_CPUPROFILER_EVENT_SCOPE(ProcessCompletedRequests);

		FIoRequestImpl* CompletedRequestsHead = FileIoStore.GetCompletedRequests();
		while (CompletedRequestsHead)
		{
			FIoRequestImpl* NextRequest = CompletedRequestsHead->NextRequest;
			CompleteRequest(CompletedRequestsHead);
			CompletedRequestsHead = NextRequest;
			--PendingIoRequestsCount;
			TRACE_COUNTER_SET(PendingIoRequests, PendingIoRequestsCount);
		}
	}

	void CompleteRequest(FIoRequestImpl* Request)
	{
		//TRACE_CPUPROFILER_EVENT_SCOPE(CompleteRequest);

		if (!Request->Status.IsCompleted())
		{
			if (Request->bFailed)
			{
				Request->Status = EIoErrorCode::ReadError;
			}
			else
			{
				Request->Status = EIoErrorCode::Ok;
			}
		}
		if (Request->Callback)
		{
			if (Request->Status.IsOk())
			{
				Request->Callback(Request->IoBuffer);
#if CSV_PROFILER && IS_MONOLITHIC
				FPlatformAtomics::InterlockedAdd(&GTotalLoaded, Request->Options.GetSize());
#endif
			}
			else
			{
				Request->Callback(Request->Status);
			}
		}

		if (Request->Batch)
		{
			check(Request->Batch->UnfinishedRequestsCount);
			if (--Request->Batch->UnfinishedRequestsCount == 0)
			{
				InvokeCallback(Request->Batch);
			}
		}
		else
		{
			FreeRequest(Request);
		}
	}

	void InvokeCallback(FIoBatchImpl* Batch)
	{	
		if (!Batch->Callback)
		{
			// No point checking if the batch does not have a callback
			return;
		}

		// If there is no valid tail request then it should not have been possible to call this method
		check(Batch->TailRequest != nullptr); 

		// Since the requests will be processed in order we can just check the tail request
		check(Batch->TailRequest->Status.IsCompleted());

		FIoStatus Status = EIoErrorCode::Ok;
		// Check the requests in the batch to see if we need to report an error status
		for (FIoRequestImpl* Request = Batch->HeadRequest; Request != NULL && Status.IsOk(); Request = Request->BatchNextRequest)
		{
			Status = Request->Status;
		}

		// Return the buffer if there are no errors, or the failed status if there were
		if (Status.IsOk())
		{
#if CSV_PROFILER && IS_MONOLITHIC
			FPlatformAtomics::InterlockedAdd(&GTotalLoaded, Batch->IoBuffer.DataSize());
#endif
			Batch->Callback(Batch->IoBuffer);

		}
		else
		{
			Batch->Callback(Status);
		}
	}

	void ProcessIncomingRequests()
	{
		FIoRequestImpl* RequestsToSubmitHead = nullptr;
		FIoRequestImpl* RequestsToSubmitTail = nullptr;
		//TRACE_CPUPROFILER_EVENT_SCOPE(ProcessIncomingRequests);
		for (;;)
		{
			{
				FScopeLock _(&WaitingLock);
				if (WaitingRequestsHead)
				{
					if (RequestsToSubmitTail)
					{
						RequestsToSubmitTail->NextRequest = WaitingRequestsHead;
						RequestsToSubmitTail = WaitingRequestsTail;
					}
					else
					{
						RequestsToSubmitHead = WaitingRequestsHead;
						RequestsToSubmitTail = WaitingRequestsTail;
					}
					WaitingRequestsHead = WaitingRequestsTail = nullptr;
				}
			}
			if (!RequestsToSubmitHead)
			{
				return;
			}

			FIoRequestImpl* Request = RequestsToSubmitHead;
			RequestsToSubmitHead = RequestsToSubmitHead->NextRequest;
			if (!RequestsToSubmitHead)
			{
				RequestsToSubmitTail = nullptr;
			}

			TRACE_CPUPROFILER_EVENT_SCOPE(ResolveRequest);

			// Make sure that the FIoChunkId in the request is valid before we try to do anything with it.
			if (Request->ChunkId.IsValid())
			{
				EIoStoreResolveResult Result = FileIoStore.Resolve(Request);
				if (Result != IoStoreResolveResult_OK)
				{
					Request->Status = ToStatus(Result);
				}
			}
			else
			{
				Request->Status = FIoStatus(EIoErrorCode::InvalidParameter, TEXT("FIoChunkId is not valid"));
				continue;
			}
			
			++PendingIoRequestsCount;
			TRACE_COUNTER_SET(PendingIoRequests, PendingIoRequestsCount);
			Request->NextRequest = nullptr;
			
			ProcessCompletedRequests();
		}
	}

	virtual bool Init()
	{
		return true;
	}

	virtual uint32 Run()
	{
		FMemory::SetupTLSCachesOnCurrentThread();
		while (!bStopRequested)
		{
			if (PendingIoRequestsCount)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(IoDispatcherWaitForIo);
				EventQueue.DispatcherWaitForIo();
			}
			else
			{
				EventQueue.DispatcherWait();
			}
			ProcessIncomingRequests();
			ProcessCompletedRequests();
		}
		return 0;
	}

	virtual void Stop()
	{
		bStopRequested = true;
		EventQueue.DispatcherNotify();
	}

	using FRequestAllocator = TBlockAllocator<FIoRequestImpl, 4096>;
	using FBatchAllocator = TBlockAllocator<FIoBatchImpl, 4096>;

	bool bIsMultithreaded;
	FIoDispatcherEventQueue EventQueue;

	FIoSignatureErrorEvent SignatureErrorEvent;
	FFileIoStore FileIoStore;
	FRequestAllocator RequestAllocator;
	FBatchAllocator BatchAllocator;
	FRunnableThread* Thread = nullptr;
	FCriticalSection WaitingLock;
	FIoRequestImpl* WaitingRequestsHead = nullptr;
	FIoRequestImpl* WaitingRequestsTail = nullptr;
	TAtomic<bool> bStopRequested { false };
	mutable FCriticalSection MountedContainersCritical;
	TArray<FIoDispatcherMountedContainer> MountedContainers;
	FIoDispatcher::FIoContainerMountedEvent ContainerMountedEvent;
	uint64 PendingIoRequestsCount = 0;
};

FIoDispatcher::FIoDispatcher()
	: Impl(new FIoDispatcherImpl(FGenericPlatformProcess::SupportsMultithreading()))
{
}

FIoDispatcher::~FIoDispatcher()
{
	delete Impl;
}

FIoStatus FIoDispatcher::Mount(const FIoStoreEnvironment& Environment, const FGuid& EncryptionKeyGuid, const FAES::FAESKey& EncryptionKey)
{
	LLM_SCOPE(ELLMTag::FileSystem);
	return Impl->Mount(Environment, EncryptionKeyGuid, EncryptionKey);
}

FIoBatch
FIoDispatcher::NewBatch()
{
	return FIoBatch(Impl, Impl->AllocBatch());
}

void
FIoDispatcher::FreeBatch(FIoBatch& Batch)
{
	Impl->FreeBatch(Batch.Impl);
	Batch.Impl = nullptr;
}

void
FIoDispatcher::ReadWithCallback(const FIoChunkId& ChunkId, const FIoReadOptions& Options, EIoDispatcherPriority Priority, FIoReadCallback&& Callback)
{
	Impl->ReadWithCallback(ChunkId, Options, Priority, MoveTemp(Callback));
}

TIoStatusOr<FIoMappedRegion>
FIoDispatcher::OpenMapped(const FIoChunkId& ChunkId, const FIoReadOptions& Options)
{
	return Impl->OpenMapped(ChunkId, Options);
}

// Polling methods
bool
FIoDispatcher::DoesChunkExist(const FIoChunkId& ChunkId) const
{
	return Impl->DoesChunkExist(ChunkId);
}

TIoStatusOr<uint64>
FIoDispatcher::GetSizeForChunk(const FIoChunkId& ChunkId) const
{
	return Impl->GetSizeForChunk(ChunkId);
}

TArray<FIoDispatcherMountedContainer>
FIoDispatcher::GetMountedContainers() const
{
	return Impl->GetMountedContainers();
}

FIoDispatcher::FIoContainerMountedEvent&
FIoDispatcher::OnContainerMounted()
{
	return Impl->OnContainerMounted();
}

FIoSignatureErrorEvent&
FIoDispatcher::GetSignatureErrorEvent()
{
	return Impl->GetSignatureErrorEvent();
}

bool
FIoDispatcher::IsInitialized()
{
	return GIoDispatcher.IsValid();
}

bool
FIoDispatcher::IsValidEnvironment(const FIoStoreEnvironment& Environment)
{
	return FFileIoStore::IsValidEnvironment(Environment);
}

FIoStatus
FIoDispatcher::Initialize()
{
	LLM_SCOPE(ELLMTag::FileSystem);
	GIoDispatcher = MakeUnique<FIoDispatcher>();

	return GIoDispatcher->Impl->Initialize();
}

void
FIoDispatcher::InitializePostSettings()
{
	LLM_SCOPE(ELLMTag::FileSystem);

	check(GIoDispatcher);
	bool bSuccess = GIoDispatcher->Impl->InitializePostSettings();
	UE_CLOG(!bSuccess, LogIoDispatcher, Fatal, TEXT("Failed to initialize IoDispatcher"));
}

void
FIoDispatcher::Shutdown()
{
	GIoDispatcher.Reset();
}

FIoDispatcher&
FIoDispatcher::Get()
{
	return *GIoDispatcher;
}

//////////////////////////////////////////////////////////////////////////

FIoBatch::FIoBatch(FIoDispatcherImpl* InDispatcher, FIoBatchImpl* InImpl)
	: Dispatcher(InDispatcher)
	, Impl(InImpl)
{
}

bool
FIoBatch::IsValid() const
{
	return Impl != nullptr;
}

FIoRequest
FIoBatch::Read(const FIoChunkId& ChunkId, FIoReadOptions Options)
{
	return FIoRequest(Dispatcher->AllocRequest(Impl, ChunkId, Options));
}

void
FIoBatch::ForEachRequest(TFunction<bool(FIoRequest&)>&& Callback)
{
	Dispatcher->IterateBatch(Impl, [&](FIoRequestImpl* InRequest) {
		FIoRequest Request(InRequest);
		return Callback(Request);
	});
}

void
FIoBatch::Issue(EIoDispatcherPriority Priority)
{
	Dispatcher->IssueBatch(Impl, Priority);
}

FIoStatus
FIoBatch::IssueWithCallback(FIoBatchReadOptions Options, EIoDispatcherPriority Priority, FIoReadCallback&& Callback)
{
	FIoStatus Status = Dispatcher->SetupBatchForContiguousRead(Impl, Options.GetTargetVa(), MoveTemp(Callback));

	if (Status.IsOk())
	{
		Dispatcher->IssueBatch(Impl, Priority);
	}
	
	return Status;
}

void
FIoBatch::Wait()
{
	while (Impl->UnfinishedRequestsCount.Load() > 0)
	{
		FPlatformProcess::Sleep(0);
	}
}

void
FIoBatch::Cancel()
{
	unimplemented();
}

//////////////////////////////////////////////////////////////////////////

bool
FIoRequest::IsOk() const
{
	return Impl->Status.IsOk();
}

FIoStatus
FIoRequest::Status() const
{
	return Impl->Status;
}

const FIoChunkId&
FIoRequest::GetChunkId() const
{
	return Impl->ChunkId;
}

TIoStatusOr<FIoBuffer>
FIoRequest::GetResult() const
{
	if (Impl->Status.IsOk())
	{
		return Impl->IoBuffer;
	}
	else
	{
		return Impl->Status;
	}
}

