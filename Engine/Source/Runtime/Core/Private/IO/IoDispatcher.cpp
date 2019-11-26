// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "IO/IoDispatcher.h"
#include "IO/IoStore.h"
#include "Async/MappedFileHandle.h"
#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/CString.h"
#include "Misc/EventPool.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/ScopeLock.h"
#include "Misc/SecureHash.h"
#include "Misc/StringBuilder.h"
#include "Misc/LazySingleton.h"
#include "Misc/CoreDelegates.h"
#include "Trace/Trace.h"

#define IODISPATCHER_TRACE_ENABLED !UE_BUILD_SHIPPING

DEFINE_LOG_CATEGORY(LogIoDispatcher);

const FIoChunkId FIoChunkId::InvalidChunkId = FIoChunkId::CreateEmptyId();

#if !defined(PLATFORM_IMPLEMENTS_IO)
//////////////////////////////////////////////////////////////////////////

TUniquePtr<FIoDispatcher> GIoDispatcher;
FIoStoreEnvironment GIoStoreEnvironment;

UE_TRACE_EVENT_BEGIN(IoDispatcher, BatchIssued, Always)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, BatchId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(IoDispatcher, BatchResolved, Always)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, BatchId)
	UE_TRACE_EVENT_FIELD(uint64, TotalSize)
UE_TRACE_EVENT_END()

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
				FElement* Element	= &Block->Elements[ElementIndex];
				Element->Next		= NextFree;
				NextFree			= Element;
			}

			Block->Next	= Blocks;
			Blocks		= Block;
		}

		FElement* Element	= NextFree;
		NextFree			= Element->Next;
		
		++NumElements;

		return Element->Buffer.GetTypedPtr();
	}

	FORCEINLINE void Free(T* Ptr)
	{
		FScopeLock _(&CriticalSection);

		FElement* Element	= reinterpret_cast<FElement*>(Ptr);
		Element->Next		= NextFree;
		NextFree			= Element;

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

		Blocks		= nullptr;
		NextFree	= nullptr;
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

	FBlock*				Blocks		= nullptr;
	FElement*			NextFree	= nullptr;
	int32				NumElements = 0;
	FCriticalSection	CriticalSection;
};

//////////////////////////////////////////////////////////////////////////

class FIoStoreReaderImpl
{
public:
	FIoStoreReaderImpl(FIoStoreEnvironment& InEnvironment)
		: Environment(InEnvironment)
	{
	}

	FIoStatus Initialize(FStringView UniqueId);

	TIoStatusOr<FIoBuffer> Lookup(const FIoChunkId& ChunkId, FIoReadOptions Options)
	{
		if (const FIoStoreTocEntry* Entry = Toc.Find(ChunkId))
		{
			return FIoBuffer(FIoBuffer::Wrap, MappedRegion->GetMappedPtr() + Entry->GetOffset(), Entry->GetLength());
		}

		return FIoStatus(EIoErrorCode::NotFound);
	}

	bool DoesChunkExist(const FIoChunkId& ChunkId) const
	{
		return Toc.Find(ChunkId) != nullptr;
	}

	TIoStatusOr<uint64> GetSizeForChunk(const FIoChunkId& ChunkId) const
	{
		const FIoStoreTocEntry* Entry = Toc.Find(ChunkId);

		if (Entry != nullptr)
		{
			return Entry->GetLength();
		}
		else
		{
			return FIoStatus(EIoErrorCode::NotFound);
		}
	}

private:
	FIoStoreEnvironment&				Environment;
	FString								UniqueId;
	TMap<FIoChunkId, FIoStoreTocEntry>	Toc;
	TUniquePtr<IFileHandle>				ContainerFileHandle;
	TUniquePtr<IMappedFileHandle>		ContainerMappedFileHandle;
	TUniquePtr<IMappedFileRegion>		MappedRegion;
};

FIoStatus FIoStoreReaderImpl::Initialize(FStringView InUniqueId)
{
	IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();

	UniqueId = InUniqueId;

	const FString& RootPath = Environment.GetRootPath();

	TStringBuilder<256> ContainerFilePath;
	ContainerFilePath.Append(RootPath);
	if (ContainerFilePath.LastChar() != '/')
		ContainerFilePath.Append(TEXT('/'));

	TStringBuilder<256> TocFilePath;
	TocFilePath.Append(ContainerFilePath);
	TocFilePath.Append(TEXT("Container.utoc"));

	ContainerFilePath.Append(TEXT("Container.ucas"));
	ContainerFileHandle.Reset(Ipf.OpenRead(*ContainerFilePath, /* allowwrite */ false));
	
	if (!ContainerFileHandle)
	{
		return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to open IoStore container file '") << *ContainerFilePath << TEXT("'");
	}

	const uint64 ContainerSize = ContainerFileHandle->Size();

	ContainerMappedFileHandle.Reset(Ipf.OpenMapped(*ContainerFilePath));
	MappedRegion.Reset(ContainerMappedFileHandle->MapRegion());

	if (!ContainerMappedFileHandle)
	{
		return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to memory map IoStore container file '") << *ContainerFilePath << TEXT("'");
	}

	TUniquePtr<uint8[]> TocBuffer;
	bool bTocReadOk = false;

	{
		TUniquePtr<IFileHandle>	TocFileHandle(Ipf.OpenRead(*TocFilePath, /* allowwrite */ false));

		if (!TocFileHandle)
		{
			return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to open IoStore TOC file '") << *TocFilePath << TEXT("'");
		}

		const int64 TocSize = TocFileHandle->Size();
		TocBuffer = MakeUnique<uint8[]>(TocSize);
		bTocReadOk = TocFileHandle->Read(TocBuffer.Get(), TocSize);
	}

	if (!bTocReadOk)
	{
		return FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("Failed to read IoStore TOC file '") << *TocFilePath << TEXT("'");
	}

	const FIoStoreTocHeader* Header = reinterpret_cast<const FIoStoreTocHeader*>(TocBuffer.Get());

	if (!Header->CheckMagic())
	{
		return FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("TOC header magic mismatch while reading '") << *TocFilePath << TEXT("'");
	}

	if (Header->TocHeaderSize != sizeof(FIoStoreTocHeader))
	{
		return FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("TOC header size mismatch while reading '") << *TocFilePath << TEXT("'");
	}

	if (Header->TocEntrySize != sizeof(FIoStoreTocEntry))
	{
		return FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("TOC entry size mismatch while reading '") << *TocFilePath << TEXT("'");
	}

	const FIoStoreTocEntry* Entry = reinterpret_cast<const FIoStoreTocEntry*>(TocBuffer.Get() + sizeof(FIoStoreTocHeader));
	uint32 EntryCount = Header->TocEntryCount;

	Toc.Reserve(EntryCount);

	while(EntryCount--)
	{
		if ((Entry->GetOffset() + Entry->GetLength()) > ContainerSize)
		{
			// TODO: add details
			return FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("TOC entry out of container bounds while reading '") << *TocFilePath << TEXT("'");
		}

		Toc.Add(Entry->ChunkId, *Entry);

		++Entry;
	}

	return FIoStatus::Ok;
}

FIoStoreReader::FIoStoreReader(FIoStoreEnvironment& Environment)
:	Impl(new FIoStoreReaderImpl(Environment))
{
}

FIoStoreReader::~FIoStoreReader()
{
	delete Impl;
}

FIoStatus 
FIoStoreReader::Initialize(FStringView UniqueId)
{
	return Impl->Initialize(UniqueId);
}

//////////////////////////////////////////////////////////////////////////

class FIoBatchImpl
{
public:
	FIoRequestImpl* FirstRequest	= nullptr;
	FIoBatchImpl*	NextBatch		= nullptr;
	TAtomic<uint32>	OutstandingRequests { 0 };
};

class FIoRequestImpl
{
public:
	FIoChunkId				ChunkId;
	FIoReadOptions			Options;
	TIoStatusOr<FIoBuffer>	Result;
	FIoRequestImpl*			NextRequest = nullptr;
};

//////////////////////////////////////////////////////////////////////////

class FIoStoreImpl : public FRefCountBase
{
public:
	void Mount(FIoStoreReader* IoStore)
	{
		FWriteScopeLock _(RwLockIoStores);
		// TODO prevent duplicates?
		IoStores.Add(IoStore);
	}

	void Unmount(FIoStoreReader* IoStore)
	{
		FWriteScopeLock _(RwLockIoStores);
		IoStores.Remove(IoStore);
	}

	TIoStatusOr<FIoBuffer> Resolve(FIoChunkId& ChunkId, FIoReadOptions Options)
	{
		for (const FIoStoreReader* IoStore : IoStores)
		{
			TIoStatusOr<FIoBuffer> Result = IoStore->Impl->Lookup(ChunkId, Options);

			if (Result.IsOk())
			{
				return Result;
			}
		}

		return FIoStatus(EIoErrorCode::NotFound);
	}

	bool DoesChunkExist(const FIoChunkId& ChunkId) const
	{
		for (const FIoStoreReader* IoStore : IoStores)
		{
			if (IoStore->Impl->DoesChunkExist(ChunkId))
			{
				return true;
			}
		}

		return false;
	}

	TIoStatusOr<uint64> GetSizeForChunk(const FIoChunkId& ChunkId) const
	{
		for (const FIoStoreReader* IoStore : IoStores)
		{
			TIoStatusOr<uint64> Result = IoStore->Impl->GetSizeForChunk(ChunkId);

			if (Result.IsOk())
			{
				return Result;
			}
		}

		return FIoStatus(EIoErrorCode::NotFound);
	}

private:
	FRWLock									RwLockIoStores;
	TArray<TRefCountPtr<FIoStoreReader>>	IoStores;
};

//////////////////////////////////////////////////////////////////////////

class FIoDispatcherImpl
{
public:
	FIoDispatcherImpl()
	{
		IoStore = new FIoStoreImpl;

		FCoreDelegates::GetMemoryTrimDelegate().AddLambda([this]()
		{
			RequestAllocator.Trim();
			BatchAllocator.Trim();
		});
	}

	FIoRequestImpl* AllocRequest(const FIoChunkId& ChunkId, FIoReadOptions Options)
	{
		FIoRequestImpl* Request = RequestAllocator.Construct();

		Request->ChunkId		= ChunkId;
		Request->Options		= Options;
		Request->Result			= FIoStatus::Unknown;
		Request->NextRequest	= nullptr;

		return Request;
	}

	FIoRequestImpl* AllocRequest(FIoBatchImpl* Batch, const FIoChunkId& ChunkId, FIoReadOptions Options)
	{
		FIoRequestImpl* Request	= AllocRequest(ChunkId, Options);

		Request->NextRequest	= Batch->FirstRequest;
		Batch->FirstRequest		= Request;

		return Request;
	}

	void FreeRequest(FIoRequestImpl* Request)
	{
		RequestAllocator.Destroy(Request);
	}

	FIoBatchImpl* AllocBatch(FIoRequestImpl* FirstRequest = nullptr)
	{
		FIoBatchImpl* Batch			= BatchAllocator.Construct();

		Batch->FirstRequest			= FirstRequest;
		Batch->OutstandingRequests	= 0;
	
		return Batch;
	}

	void FreeBatch(FIoBatchImpl* Batch)
	{
		FIoRequestImpl* Request = Batch->FirstRequest;

		while (Request)
		{
			FIoRequestImpl* Tmp = Request;	
			Request = Request->NextRequest;
			FreeRequest(Tmp);
		}

		BatchAllocator.Destroy(Batch);
	}

	void ReadWithCallback(const FIoChunkId& Chunk, const FIoReadOptions& Options, TFunction<void(TIoStatusOr<FIoBuffer>)>&& Callback)
	{
		FIoRequestImpl* Request = AllocRequest(Chunk, Options);
		Request->Result = IoStore->Resolve(Request->ChunkId, Request->Options);
		if (Request->Result.IsOk())
		{
			Callback(Request->Result.ConsumeValueOrDie());
		}
		else
		{
			Callback(TIoStatusOr<FIoBuffer>(Request->Result.Status()));
		}
		FreeRequest(Request);
	}

	bool DoesChunkExist(const FIoChunkId& ChunkId) const
	{
		return IoStore->DoesChunkExist(ChunkId);
	}

	TIoStatusOr<uint64> GetSizeForChunk(const FIoChunkId& ChunkId) const
	{
		return IoStore->GetSizeForChunk(ChunkId);
	}

	template<typename Func>
	void IterateBatch(const FIoBatchImpl* Batch, Func&& InCallbackFunction)
	{
		FIoRequestImpl* Request = Batch->FirstRequest;

		while (Request)
		{
			const bool bDoContinue = InCallbackFunction(Request);

			Request = bDoContinue ? Request->NextRequest : nullptr;
		}
	}

	void IssueBatch(const FIoBatchImpl* Batch)
	{
		// At this point the batch is immutable and we should start
		// doing the work.

#if IODISPATCHER_TRACE_ENABLED
		UE_TRACE_LOG(IoDispatcher, BatchIssued)
			<< BatchIssued.Cycle(FPlatformTime::Cycles64())
			<< BatchIssued.BatchId(uint64(Batch));
#endif
		uint64 TotalBatchSize = 0;
		IterateBatch(Batch, [this, &TotalBatchSize](FIoRequestImpl* Request)
		{
			Request->Result = IoStore->Resolve(Request->ChunkId, Request->Options);

#if IODISPATCHER_TRACE_ENABLED
			TotalBatchSize += Request->Result.ValueOrDie().DataSize();
#endif
			return true;
		});

#if IODISPATCHER_TRACE_ENABLED
		UE_TRACE_LOG(IoDispatcher, BatchResolved)
			<< BatchResolved.Cycle(FPlatformTime::Cycles64())
			<< BatchResolved.BatchId(uint64(Batch))
			<< BatchResolved.TotalSize(TotalBatchSize);
#endif
	}

	bool IsBatchReady(const FIoBatchImpl* Batch)
	{
		bool bIsReady = true;

		IterateBatch(Batch, [&bIsReady](FIoRequestImpl* Request)
		{
			bIsReady &= Request->Result.Status().IsCompleted();
			return bIsReady;
		});

		return bIsReady;
	}

	void Mount(FIoStoreReader* IoStoreReader)
	{
		IoStore->Mount(IoStoreReader);
	}

	void Unmount(FIoStoreReader* IoStoreReader)
	{
		IoStore->Unmount(IoStoreReader);
	}

private:
	using FRequestAllocator		= TBlockAllocator<FIoRequestImpl, 4096>;
	using FBatchAllocator		= TBlockAllocator<FIoBatchImpl, 4096>;

	TRefCountPtr<FIoStoreImpl>	IoStore;
	FRequestAllocator			RequestAllocator;
	FBatchAllocator				BatchAllocator;
};

//////////////////////////////////////////////////////////////////////////

FIoDispatcher::FIoDispatcher()
:	Impl(new FIoDispatcherImpl())
{
}

FIoDispatcher::~FIoDispatcher()
{
	delete Impl;
}

void		
FIoDispatcher::Mount(FIoStoreReader* IoStore)
{
	Impl->Mount(IoStore);
}

void		
FIoDispatcher::Unmount(FIoStoreReader* IoStore)
{
	Impl->Unmount(IoStore);
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

FIoStatus
FIoDispatcher::Initialize(const FString& Directory)
{
	GIoStoreEnvironment.InitializeFileEnvironment(Directory);
	GIoDispatcher = MakeUnique<FIoDispatcher>();

	return EIoErrorCode::Ok;
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

FIoStoreEnvironment&
FIoDispatcher::GetEnvironment()
{
	return GIoStoreEnvironment;
}

//////////////////////////////////////////////////////////////////////////

FIoBatch::FIoBatch(FIoDispatcherImpl* InDispatcher, FIoBatchImpl* InImpl)
:	Dispatcher(InDispatcher)
,	Impl(InImpl)
,	CompletionEvent()
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
	//TODO: Create synchronization event here when it's actually needed
	unimplemented();
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
	return Impl->Result.IsOk(); 
}

FIoStatus	
FIoRequest::Status() const
{ 
	return Impl->Result.Status();
}

const FIoChunkId&
FIoRequest::GetChunkId() const
{
	return Impl->ChunkId;
}

TIoStatusOr<FIoBuffer>
FIoRequest::GetResult() const
{
	return Impl->Result;
}

#endif // PLATFORM_IMPLEMENTS_IO
