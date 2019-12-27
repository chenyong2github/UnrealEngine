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

DEFINE_LOG_CATEGORY(LogIoDispatcher);

const FIoChunkId FIoChunkId::InvalidChunkId = FIoChunkId::CreateEmptyId();

TUniquePtr<FIoDispatcher> GIoDispatcher;

#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
//PRAGMA_DISABLE_OPTIMIZATION
#endif

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
	FIoDispatcherImpl()
		: FileIoStore(EventQueue)
	{
		FCoreDelegates::GetMemoryTrimDelegate().AddLambda([this]()
		{
			RequestAllocator.Trim();
			BatchAllocator.Trim();
		});

		Thread = FRunnableThread::Create(this, TEXT("IoDispatcher"), 0, TPri_AboveNormal);
	}

	~FIoDispatcherImpl()
	{
		IPlatformChunkInstall* ChunkInstall = FPlatformMisc::GetPlatformChunkInstall();
		if (ChunkInstall && ChunkDownloadedDelegateHandle.IsValid())
		{
			ChunkInstall->RemoveChunkInstallDelegate(ChunkDownloadedDelegateHandle);
		}
	}

	FIoStatus Initialize(const FIoStoreEnvironment& InitialEnvironment)
	{
		FIoStatus IoStatus = Mount(InitialEnvironment);
		if (!IoStatus.IsOk())
		{
			return IoStatus;
		}

		FIoBuffer IoBuffer;
		FEvent* Event = FPlatformProcess::GetSynchEventFromPool();
		ReadWithCallback(
			CreateIoChunkId(0, 0, EIoChunkType::InstallManifest),
			FIoReadOptions(),
			[Event, &IoBuffer](TIoStatusOr<FIoBuffer> Result)
		{
			if (Result.IsOk())
			{
				IoBuffer = Result.ConsumeValueOrDie();
			}
			Event->Trigger();
		});
		Event->Wait();
		if (!IoBuffer.DataSize())
		{
			return FIoStatusBuilder(EIoErrorCode::NotFound) << TEXT("Failed to open install manifest");
		}
		FLargeMemoryReader InstallManifestAr(IoBuffer.Data(), IoBuffer.DataSize());

		FIoStoreInstallManifest InstallManifest;
		InstallManifestAr << InstallManifest;

		IPlatformChunkInstall* ChunkInstall = FPlatformMisc::GetPlatformChunkInstall();
		if (ChunkInstall)
		{
			ChunkDownloadedDelegateHandle = ChunkInstall->AddChunkInstallDelegate(FPlatformChunkInstallDelegate::CreateRaw(this, &FIoDispatcherImpl::OnChunkDownloaded));
		}
		for (const FIoStoreInstallManifest::FEntry& InstallManifestEntry : InstallManifest.ReadEntries())
		{
			FIoStoreEnvironment PartitionEnvironment(InitialEnvironment, InstallManifestEntry.PartitionName);
			if (ChunkInstall && ChunkInstall->GetChunkLocation(InstallManifestEntry.InstallChunkId) == EChunkLocation::NotAvailable)
			{
				PendingInstallChunks.Add(MakeTuple(InstallManifestEntry.InstallChunkId, PartitionEnvironment));
			}
			else
			{
				Mount(PartitionEnvironment);
			}
		}

		return FIoStatus::Ok;
	}

	FIoRequestImpl* AllocRequest(const FIoChunkId& ChunkId, FIoReadOptions Options)
	{
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
		Request->BatchNextRequest = Batch->FirstRequest;
		Batch->FirstRequest = Request;

		return Request;
	}

	void FreeRequest(FIoRequestImpl* Request)
	{
		RequestAllocator.Destroy(Request);
	}

	FIoBatchImpl* AllocBatch()
	{
		FIoBatchImpl* Batch = BatchAllocator.Construct();

		return Batch;
	}

	void FreeBatch(FIoBatchImpl* Batch)
	{
		FIoRequestImpl* Request = Batch->FirstRequest;

		while (Request)
		{
			FIoRequestImpl* Tmp = Request;
			Request = Request->BatchNextRequest;
			FreeRequest(Tmp);
		}

		BatchAllocator.Destroy(Batch);
	}

	void ReadWithCallback(const FIoChunkId& Chunk, const FIoReadOptions& Options, TFunction<void(TIoStatusOr<FIoBuffer>)>&& Callback)
	{
		FIoRequestImpl* Request = AllocRequest(Chunk, Options);
		Request->Callback = MoveTemp(Callback);
		{
			FScopeLock _(&WaitingLock);
			Request->NextRequest = nullptr;
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
		EventQueue.Notify();
	}

	FIoStatus Mount(const FIoStoreEnvironment& Environment)
	{
		return FileIoStore.Mount(Environment);
	}

	bool DoesChunkExist(const FIoChunkId& ChunkId) const
	{
		return FileIoStore.DoesChunkExist(ChunkId);
	}

	TIoStatusOr<uint64> GetSizeForChunk(const FIoChunkId& ChunkId) const
	{
		return FileIoStore.GetSizeForChunk(ChunkId);
	}

	template<typename Func>
	void IterateBatch(const FIoBatchImpl* Batch, Func&& InCallbackFunction)
	{
		FIoRequestImpl* Request = Batch->FirstRequest;

		while (Request)
		{
			const bool bDoContinue = InCallbackFunction(Request);

			Request = bDoContinue ? Request->BatchNextRequest : nullptr;
		}
	}

	void IssueBatch(const FIoBatchImpl* Batch)
	{
		{
			FScopeLock _(&WaitingLock);
			if (!WaitingRequestsHead)
			{
				WaitingRequestsHead = Batch->FirstRequest;
			}
			else
			{
				WaitingRequestsTail->NextRequest = Batch->FirstRequest;
			}
			WaitingRequestsTail = Batch->FirstRequest;
			while (WaitingRequestsTail->BatchNextRequest)
			{
				WaitingRequestsTail->NextRequest = WaitingRequestsTail->BatchNextRequest;
				WaitingRequestsTail = WaitingRequestsTail->BatchNextRequest;
			}
			WaitingRequestsTail->NextRequest = nullptr;
		}
		EventQueue.Notify();
	}

private:
	friend class FIoBatch;

	void CompleteRequests()
	{
		while (SubmittedRequestsHead && SubmittedRequestsHead->UnfinishedReadsCount == 0)
		{
			FIoRequestImpl* NextRequest = SubmittedRequestsHead->NextRequest;
			CompleteRequest(SubmittedRequestsHead);
			SubmittedRequestsHead = NextRequest;
		}
		if (!SubmittedRequestsHead)
		{
			SubmittedRequestsTail = nullptr;
		}
	}

	void CompleteRequest(FIoRequestImpl* Request)
	{
		if (!Request->Status.IsCompleted())
		{
			Request->Status = EIoErrorCode::Ok;
			if (Request->Callback)
			{
				Request->Callback(Request->IoBuffer);
			}
		}
		else
		{
			if (Request->Callback)
			{
				Request->Callback(Request->Status);
			}
		}
		if (!Request->Batch)
		{
			FreeRequest(Request);
		}
	}

	void ProcessIncomingRequests()
	{
		FIoRequestImpl* WaitingRequest;
		{
			FScopeLock _(&WaitingLock);
			WaitingRequest = WaitingRequestsHead;
			WaitingRequestsHead = WaitingRequestsTail = nullptr;
		}
		while (WaitingRequest)
		{
			RequestsToSubmit.Add(WaitingRequest);
			WaitingRequest = WaitingRequest->NextRequest;
		}

		int32 RequestsToSubmitCount = RequestsToSubmit.Num();
		if (!RequestsToSubmitCount)
		{
			return;
		}

		//TRACE_CPUPROFILER_EVENT_SCOPE(IoDispatcherSubmitRequests);

		for (; CurrentRequestsToSubmitIndex < RequestsToSubmitCount; ++CurrentRequestsToSubmitIndex)
		{
			FIoRequestImpl* Request = RequestsToSubmit[CurrentRequestsToSubmitIndex];
			check(Request);

			EIoStoreResolveResult Result = FileIoStore.Resolve(Request);
			if (Result == IoStoreResolveResult_Stalled)
			{
				break;
			}
			if (Result == IoStoreResolveResult_NotFound)
			{
				Request->Status = FIoStatus(EIoErrorCode::NotFound);
			}
			if (!SubmittedRequestsTail)
			{
				SubmittedRequestsHead = SubmittedRequestsTail = Request;
			}
			else
			{
				SubmittedRequestsTail->NextRequest = Request;
				SubmittedRequestsTail = Request;
			}
			Request->NextRequest = nullptr;
		}

		if (CurrentRequestsToSubmitIndex == RequestsToSubmitCount)
		{
			RequestsToSubmit.Reset(false);
			CurrentRequestsToSubmitIndex = 0;
		}
	}

	void OnChunkDownloaded(uint32 ChunkId, bool bSuccess)
	{
		if (bSuccess)
		{
			for (auto It = PendingInstallChunks.CreateIterator(); It; ++It)
			{
				int32 PendingChunkId = It->Get<0>();
				if (PendingChunkId == ChunkId)
				{
					const FIoStoreEnvironment& PendingEnvironment = It->Get<1>();
					Mount(PendingEnvironment);
					It.RemoveCurrent();
				}
			}
		}
	}

	virtual bool Init()
	{
		return true;
	}

	virtual uint32 Run()
	{
		while (true)
		{
			EventQueue.ProcessEvents();

			FileIoStore.ProcessIncomingBlocks();
			ProcessIncomingRequests();
			CompleteRequests();
		}
		return 0;
	}

	virtual void Stop()
	{
	}

	using FRequestAllocator = TBlockAllocator<FIoRequestImpl, 4096>;
	using FBatchAllocator = TBlockAllocator<FIoBatchImpl, 4096>;

	FIoDispatcherEventQueue EventQueue;

	FFileIoStore FileIoStore;
	FRequestAllocator RequestAllocator;
	FBatchAllocator BatchAllocator;
	FRunnableThread* Thread = nullptr;
	FCriticalSection WaitingLock;
	FIoRequestImpl* WaitingRequestsHead = nullptr;
	FIoRequestImpl* WaitingRequestsTail = nullptr;
	TArray<FIoRequestImpl*> RequestsToSubmit;
	int32 CurrentRequestsToSubmitIndex = 0;
	FIoRequestImpl* SubmittedRequestsHead = nullptr;
	FIoRequestImpl* SubmittedRequestsTail = nullptr;
	FDelegateHandle ChunkDownloadedDelegateHandle;
	TArray<TTuple<int32, FIoStoreEnvironment>> PendingInstallChunks;
};

FIoDispatcher::FIoDispatcher()
	: Impl(new FIoDispatcherImpl())
{
}

FIoDispatcher::~FIoDispatcher()
{
	delete Impl;
}

FIoStatus FIoDispatcher::Mount(const FIoStoreEnvironment& Environment)
{
	return Impl->Mount(Environment);
}

FIoBatch
FIoDispatcher::NewBatch()
{
	return FIoBatch(Impl, Impl->AllocBatch());
}

void
FIoDispatcher::FreeBatch(FIoBatch Batch)
{
	Impl->FreeBatch(Batch.Impl);
}

void
FIoDispatcher::ReadWithCallback(const FIoChunkId& Chunk, const FIoReadOptions& Options, TFunction<void(TIoStatusOr<FIoBuffer>)>&& Callback)
{
	Impl->ReadWithCallback(Chunk, Options, MoveTemp(Callback));
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
FIoDispatcher::Initialize(const FIoStoreEnvironment& InitialEnvironment)
{
	GIoDispatcher = MakeUnique<FIoDispatcher>();

	return GIoDispatcher->Impl->Initialize(InitialEnvironment);
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

FIoBatch::FIoBatch(FIoDispatcherImpl* InDispatcher, FIoBatchImpl* InImpl)
	: Dispatcher(InDispatcher)
	, Impl(InImpl)
	, CompletionEvent()
{
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
FIoBatch::Issue()
{
	Dispatcher->IssueBatch(Impl);
}

void
FIoBatch::Wait()
{
	for (FIoRequestImpl* Request = Impl->FirstRequest; Request; Request = Request->BatchNextRequest)
	{
		while (!Request->Status.IsCompleted())
		{
			FPlatformProcess::Sleep(0);
		}
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
