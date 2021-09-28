// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/EditorDerivedDataIoStore.h"

#if WITH_EDITORONLY_DATA

#include "Containers/Map.h"
#include "DerivedDataRequestOwner.h"
#include "DerivedDataRequestTypes.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeRWLock.h"
#include "Serialization/EditorDerivedData.h"
#include <atomic>

namespace UE::DerivedData::IoStore
{

static EPriority GetPriority(int32 Priority)
{
	if (Priority < IoDispatcherPriority_Low)
	{
		return EPriority::Lowest;
	}
	if (Priority > IoDispatcherPriority_High)
	{
		return EPriority::Highest;
	}
	if (Priority < IoDispatcherPriority_Medium)
	{
		return EPriority::Low;
	}
	if (Priority > IoDispatcherPriority_Medium)
	{
		return EPriority::High;
	}
	return EPriority::Normal;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FDerivedDataIoRequestQueue
{
public:
	void Add(FIoRequestImpl* Request);

	FIoRequestImpl* Steal();

	void SetContext(const TSharedRef<const FIoDispatcherBackendContext>& Context);

	void SetSkipWakeDispatcher(bool bValue) { bSkipWakeDispatcher.store(bValue, std::memory_order_relaxed); }

private:
	FRWLock Lock;
	FIoRequestImpl* Head = nullptr;
	FIoRequestImpl* Tail = nullptr;
	TSharedPtr<const FIoDispatcherBackendContext> Context;
	std::atomic<bool> bSkipWakeDispatcher{false};
};

void FDerivedDataIoRequestQueue::Add(FIoRequestImpl* Request)
{
	check(Request);
	bool bWakeDispatcher;
	{
		FWriteScopeLock WriteLock(Lock);
		if (!Tail)
		{
			Head = Request;
			Tail = Request;
		}
		else
		{
			Tail->NextRequest = Request;
			Tail = Request;
		}
		Tail->NextRequest = nullptr;
		bWakeDispatcher = !bSkipWakeDispatcher.load(std::memory_order_relaxed);
	}
	if (bWakeDispatcher)
	{
		Context->WakeUpDispatcherThreadDelegate.Execute();
	}
}

FIoRequestImpl* FDerivedDataIoRequestQueue::Steal()
{
	FWriteScopeLock WriteLock(Lock);
	FIoRequestImpl* Queue = Head;
	Head = Tail = nullptr;
	return Queue;
}

void FDerivedDataIoRequestQueue::SetContext(const TSharedRef<const FIoDispatcherBackendContext>& InContext)
{
	FWriteScopeLock WriteLock(Lock);
	Context = InContext;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FDerivedDataIoRequest::FDerivedDataIoRequest(FIoRequestImpl* InRequest, FDerivedDataIoRequestQueue* InQueue)
	: Request(InRequest)
	, Queue(InQueue)
{
}

FRequestOwner& FDerivedDataIoRequest::GetOwner()
{
	if (!Request->BackendData)
	{
		static_assert(sizeof(Request->BackendData) == sizeof(FRequestOwner));
		new(&Request->BackendData) FRequestOwner(GetPriority(Request->Priority));
	}
	return reinterpret_cast<FRequestOwner&>(Request->BackendData);
}

FMutableMemoryView FDerivedDataIoRequest::CreateBuffer(uint64 Size)
{
	if (!Request->HasBuffer())
	{
		Request->CreateBuffer(Size);
	}
	FIoBuffer& Buffer = Request->GetBuffer();
	return MakeMemoryView(Buffer.Data(), Buffer.DataSize());
}

uint64 FDerivedDataIoRequest::GetOffset() const
{
	return Request->Options.GetOffset();
}

uint64 FDerivedDataIoRequest::GetSize() const
{
	return Request->Options.GetSize();
}

void FDerivedDataIoRequest::SetComplete()
{
	Queue->Add(Request);
}

void FDerivedDataIoRequest::SetFailed()
{
	Request->SetFailed();
	Queue->Add(Request);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FEditorDerivedDataIoStore final : public IEditorDerivedDataIoStore
{
public:
	void Initialize(TSharedRef<const FIoDispatcherBackendContext> Context) final;
	bool Resolve(FIoRequestImpl* Request) final;
	void CancelIoRequest(FIoRequestImpl* Request) final;
	void UpdatePriorityForIoRequest(FIoRequestImpl* Request) final;
	bool DoesChunkExist(const FIoChunkId& ChunkId) const final;
	TIoStatusOr<uint64> GetSizeForChunk(const FIoChunkId& ChunkId) const final;
	FIoRequestImpl* GetCompletedRequests() final;
	TIoStatusOr<FIoMappedRegion> OpenMapped(const FIoChunkId& ChunkId, const FIoReadOptions& Options) final;

	FIoChunkId AddData(const Private::FEditorDerivedData* Data) final;
	const Private::FEditorDerivedData* RemoveData(const FIoChunkId& ChunkId) final;

private:
	mutable FRWLock DataLock;
	TMap<FIoChunkId, const Private::FEditorDerivedData*> DataById;
	uint64 CurrentChunkId = 0;
	uint16 LastChunkIndex = 0;
	FDerivedDataIoRequestQueue CompletedQueue;
};

void FEditorDerivedDataIoStore::Initialize(TSharedRef<const FIoDispatcherBackendContext> Context)
{
	CompletedQueue.SetContext(Context);
}

bool FEditorDerivedDataIoStore::Resolve(FIoRequestImpl* Request)
{
	if (Request->ChunkId.GetChunkType() != EIoChunkType::EditorDerivedData)
	{
		return false;
	}

	FReadScopeLock ReadLock(DataLock);
	if (const Private::FEditorDerivedData* Data = DataById.FindRef(Request->ChunkId))
	{
		CompletedQueue.SetSkipWakeDispatcher(true);
		Data->Read(FDerivedDataIoRequest(Request, &CompletedQueue));
		CompletedQueue.SetSkipWakeDispatcher(false);
		return true;
	}
	return false;
}

void FEditorDerivedDataIoStore::CancelIoRequest(FIoRequestImpl* Request)
{
	FDerivedDataIoRequest(Request, &CompletedQueue).GetOwner().Cancel();
}

void FEditorDerivedDataIoStore::UpdatePriorityForIoRequest(FIoRequestImpl* Request)
{
	FDerivedDataIoRequest(Request, &CompletedQueue).GetOwner().SetPriority(GetPriority(Request->Priority));
}

bool FEditorDerivedDataIoStore::DoesChunkExist(const FIoChunkId& ChunkId) const
{
	if (ChunkId.GetChunkType() != EIoChunkType::EditorDerivedData)
	{
		return false;
	}

	FReadScopeLock ReadLock(DataLock);
	return DataById.Contains(ChunkId);
}

TIoStatusOr<uint64> FEditorDerivedDataIoStore::GetSizeForChunk(const FIoChunkId& ChunkId) const
{
	FReadScopeLock ReadLock(DataLock);
	if (const Private::FEditorDerivedData* Data = DataById.FindRef(ChunkId))
	{
		uint64 Size;
		if (Data->TryGetSize(Size))
		{
			return Size;
		}
	}
	return FIoStatus(EIoErrorCode::NotFound);
}

FIoRequestImpl* FEditorDerivedDataIoStore::GetCompletedRequests()
{
	FIoRequestImpl* const Head = CompletedQueue.Steal();
	for (FIoRequestImpl* Request = Head; Request; Request = Request->NextRequest)
	{
		if (Request->BackendData)
		{
			FDerivedDataIoRequest(Request, &CompletedQueue).GetOwner().~FRequestOwner();
			Request->BackendData = nullptr;
		}
	}
	return Head;
}

TIoStatusOr<FIoMappedRegion> FEditorDerivedDataIoStore::OpenMapped(const FIoChunkId& ChunkId, const FIoReadOptions& Options)
{
	return FIoStatus(EIoErrorCode::NotFound);
}

FIoChunkId FEditorDerivedDataIoStore::AddData(const Private::FEditorDerivedData* Data)
{
	FWriteScopeLock WriteLock(DataLock);
	if (LastChunkIndex == MAX_uint16)
	{
		++CurrentChunkId;
	}
	const FIoChunkId ChunkId = CreateIoChunkId(CurrentChunkId, ++LastChunkIndex, EIoChunkType::EditorDerivedData);
	DataById.Add(ChunkId, Data);
	return ChunkId;
}

const Private::FEditorDerivedData* FEditorDerivedDataIoStore::RemoveData(const FIoChunkId& ChunkId)
{
	FWriteScopeLock WriteLock(DataLock);
	return DataById.FindAndRemoveChecked(ChunkId);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<IEditorDerivedDataIoStore> CreateEditorDerivedDataIoStore()
{
	return MakeShared<FEditorDerivedDataIoStore>();
}

} // UE::DerivedData::IoStore

#endif // WITH_EDITORONLY_DATA
