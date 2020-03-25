// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/IoDispatcherFileBackend.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/StringBuilder.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "HAL/PlatformFilemanager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/IConsoleManager.h"

TRACE_DECLARE_MEMORY_COUNTER(IoDispatcherTotalBytesRead, TEXT("IoDispatcher/TotalBytesRead"));
TRACE_DECLARE_MEMORY_COUNTER(IoDispatcherTotalBytesScattered, TEXT("IoDispatcher/TotalBytesScattered"));
TRACE_DECLARE_INT_COUNTER(IoDispatcherCacheHitsCold, TEXT("IoDispatcher/CacheHitsCold"));
TRACE_DECLARE_INT_COUNTER(IoDispatcherCacheHitsHot, TEXT("IoDispatcher/CacheHitsHot"));
TRACE_DECLARE_INT_COUNTER(IoDispatcherCacheMisses, TEXT("IoDispatcher/CacheMisses"));

//PRAGMA_DISABLE_OPTIMIZATION

int32 GIoDispatcherBlockSizeKB = 256;
/*static FAutoConsoleVariableRef CVar_IoDispatcherBlockSizeKB(
	TEXT("s.IoDispatcherBlockSizeKB"),
	GIoDispatcherBlockSizeKB,
	TEXT("IoDispatcher read block size (in kilobytes).")
);*/

int32 GIoDispatcherCacheSizeMB = 32;
static FAutoConsoleVariableRef CVar_IoDispatcherCacheSizeMB(
	TEXT("s.IoDispatcherCacheSizeMB"),
	GIoDispatcherCacheSizeMB,
	TEXT("IoDispatcher cache size (in megabytes).")
);

FFileIoStoreReader::FFileIoStoreReader(FFileIoStoreImpl& InPlatformImpl)
	: PlatformImpl(InPlatformImpl)
{
}

FIoStatus FFileIoStoreReader::Initialize(const FIoStoreEnvironment& Environment)
{
	IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();

	TStringBuilder<256> ContainerFilePath;
	ContainerFilePath.Append(Environment.GetPath());

	TStringBuilder<256> TocFilePath;
	TocFilePath.Append(ContainerFilePath);

	ContainerFilePath.Append(TEXT(".ucas"));
	TocFilePath.Append(TEXT(".utoc"));

	if (!PlatformImpl.OpenContainer(*ContainerFilePath, ContainerFileHandle, ContainerFileSize))
	{
		return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to open IoStore container file '") << *ContainerFilePath << TEXT("'");
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
	while (EntryCount--)
	{
		if ((Entry->GetOffset() + Entry->GetLength()) > ContainerFileSize)
		{
			// TODO: add details
			return FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("TOC entry out of container bounds while reading '") << *TocFilePath << TEXT("'");
		}

		Toc.Add(Entry->ChunkId, Entry->OffsetAndLength);
		++Entry;
	}

	return FIoStatus::Ok;
}

bool FFileIoStoreReader::DoesChunkExist(const FIoChunkId& ChunkId) const
{
	return Toc.Find(ChunkId) != nullptr;
}

TIoStatusOr<uint64> FFileIoStoreReader::GetSizeForChunk(const FIoChunkId& ChunkId) const
{
	const FIoOffsetAndLength* OffsetAndLength = Toc.Find(ChunkId);

	if (OffsetAndLength != nullptr)
	{
		return OffsetAndLength->GetLength();
	}
	else
	{
		return FIoStatus(EIoErrorCode::NotFound);
	}
}

bool FFileIoStoreReader::Resolve(FFileIoStoreResolvedRequest& ResolvedRequest)
{
	const FIoOffsetAndLength* OffsetAndLength = Toc.Find(ResolvedRequest.Request->ChunkId);

	if (!OffsetAndLength)
	{
		return false;
	}

	ResolvedRequest.ResolvedFileHandle = ContainerFileHandle;
	ResolvedRequest.ResolvedFileSize = ContainerFileSize;
	uint64 RequestedOffset = ResolvedRequest.Request->Options.GetOffset();
	ResolvedRequest.ResolvedOffset = OffsetAndLength->GetOffset() + RequestedOffset;
	if (RequestedOffset > OffsetAndLength->GetLength())
	{
		ResolvedRequest.ResolvedSize = 0;
	}
	else
	{
		ResolvedRequest.ResolvedSize = FMath::Min(ResolvedRequest.Request->Options.GetSize(), OffsetAndLength->GetLength() - RequestedOffset);
	}
	return true;
}

FFileIoStore::FFileIoStore(FIoDispatcherEventQueue& InEventQueue)
	: PlatformImpl(InEventQueue)
	, CacheBlockSize(GIoDispatcherBlockSizeKB > 0 ? uint64(GIoDispatcherBlockSizeKB) << 10 : 256 << 10)
{
	LruHead.LruNext = &LruTail;
	LruTail.LruPrev = &LruHead;
}

FIoStatus FFileIoStore::Mount(const FIoStoreEnvironment& Environment)
{
	TUniquePtr<FFileIoStoreReader> Reader(new FFileIoStoreReader(PlatformImpl));
	FIoStatus IoStatus = Reader->Initialize(Environment);
	if (IoStatus.IsOk())
	{
		FWriteScopeLock _(IoStoreReadersLock);
		IoStoreReaders.Add(Reader.Release());
	}
	return IoStatus;
}

EIoStoreResolveResult FFileIoStore::Resolve(FIoRequestImpl* Request)
{
	FReadScopeLock _(IoStoreReadersLock);
	FFileIoStoreResolvedRequest ResolvedRequest;
	ResolvedRequest.Request = Request;
	for (FFileIoStoreReader* Reader : IoStoreReaders)
	{
		if (Reader->Resolve(ResolvedRequest))
		{
			Request->UnfinishedReadsCount = 0;
			if (ResolvedRequest.ResolvedSize > 0)
			{
				if (void* TargetVa = Request->Options.GetTargetVa())
				{
					ResolvedRequest.Request->IoBuffer = FIoBuffer(FIoBuffer::Wrap, TargetVa, ResolvedRequest.ResolvedSize);
				}
				PlatformImpl.BeginReadsForRequest(ResolvedRequest);
				const uint32 RequestBeginBlockIndex = (uint32)(ResolvedRequest.ResolvedOffset / CacheBlockSize);
				const uint32 RequestEndBlockIndex = (uint32)((ResolvedRequest.ResolvedOffset + ResolvedRequest.ResolvedSize - 1) / CacheBlockSize + 1);
				const uint32 BlockCount = RequestEndBlockIndex - RequestBeginBlockIndex;
				check(BlockCount > 0);
				ReadBlockCached(RequestBeginBlockIndex, ResolvedRequest);
				if (BlockCount > 1)
				{
					if (BlockCount > 2)
					{
						ReadBlocksUncached(RequestBeginBlockIndex + 1, BlockCount - 2, ResolvedRequest);
					}
					ReadBlockCached(RequestEndBlockIndex - 1, ResolvedRequest);
				}
				PlatformImpl.EndReadsForRequest();
			}

			return IoStoreResolveResult_OK;
		}
	}

	return IoStoreResolveResult_NotFound;
}

bool FFileIoStore::DoesChunkExist(const FIoChunkId& ChunkId) const
{
	FReadScopeLock _(IoStoreReadersLock);
	for (FFileIoStoreReader* Reader : IoStoreReaders)
	{
		if (Reader->DoesChunkExist(ChunkId))
		{
			return true;
		}
	}
	return false;
}

TIoStatusOr<uint64> FFileIoStore::GetSizeForChunk(const FIoChunkId& ChunkId) const
{
	FReadScopeLock _(IoStoreReadersLock);
	for (FFileIoStoreReader* Reader : IoStoreReaders)
	{
		TIoStatusOr<uint64> ReaderResult = Reader->GetSizeForChunk(ChunkId);
		if (ReaderResult.IsOk())
		{
			return ReaderResult;
		}
	}
	return FIoStatus(EIoErrorCode::NotFound);
}

bool FFileIoStore::IsValidEnvironment(const FIoStoreEnvironment& Environment)
{
	TStringBuilder<256> TocFilePath;
	TocFilePath.Append(Environment.GetPath());
	TocFilePath.Append(TEXT(".utoc"));
	return FPlatformFileManager::Get().GetPlatformFile().FileExists(*TocFilePath);
}

bool FFileIoStore::ProcessCompletedBlock()
{
	const uint64 CacheMemorySize = GIoDispatcherCacheSizeMB > 0 ? uint64(GIoDispatcherCacheSizeMB) << 20 : 0;
	FFileIoStoreReadBlock* CompletedBlock = PlatformImpl.GetNextCompletedBlock();
	if (!CompletedBlock)
	{
		return false;
	}
	check(!CompletedBlock->bIsReady);
	CompletedBlock->bIsReady = true;
	TRACE_COUNTER_ADD(IoDispatcherTotalBytesRead, CompletedBlock->Size);
	for (FFileIoStoreReadBlockScatter& Scatter : CompletedBlock->ScatterList)
	{
		if (Scatter.DstOffset != MAX_uint64)
		{
			FMemory::Memcpy(Scatter.Request->IoBuffer.Data() + Scatter.DstOffset, CompletedBlock->Buffer.Data() + Scatter.SrcOffset, Scatter.Size);
		}
		TRACE_COUNTER_ADD(IoDispatcherTotalBytesScattered, Scatter.Size);
		check(Scatter.Request->UnfinishedReadsCount > 0);
		--Scatter.Request->UnfinishedReadsCount;
	}
	//CompletedBlock->ScatterList.Empty();

	if (CompletedBlock->LruPrev)
	{
		CurrentCacheUsage += CompletedBlock->Size;

		FFileIoStoreReadBlock* EvictionCandidate = LruTail.LruPrev;
		while (CurrentCacheUsage > CacheMemorySize && EvictionCandidate != &LruHead && EvictionCandidate->bIsReady)
		{
			FFileIoStoreReadBlock* NextEvictionCandidate = EvictionCandidate->LruPrev;
			EvictionCandidate->LruNext->LruPrev = EvictionCandidate->LruPrev;
			EvictionCandidate->LruPrev->LruNext = EvictionCandidate->LruNext;
			CurrentCacheUsage -= EvictionCandidate->Size;
			CachedBlocksMap.Remove(EvictionCandidate->Key);
			delete EvictionCandidate;
			EvictionCandidate = NextEvictionCandidate;
		}
	}
	else
	{
		CachedBlocksMap.Remove(CompletedBlock->Key);
		delete CompletedBlock;
	}
	return true;
}

void FFileIoStore::ReadBlockCached(uint32 BlockIndex, const FFileIoStoreResolvedRequest& ResolvedRequest)
{
	FFileIoStoreCacheBlockKey Key;
	Key.FileHandle = ResolvedRequest.ResolvedFileHandle;
	Key.BlockIndex = BlockIndex;
	uint64 BlockOffset = uint64(BlockIndex) * uint64(CacheBlockSize);
	FFileIoStoreReadBlock* CachedBlock = CachedBlocksMap.FindRef(Key);
	if (!CachedBlock)
	{
		uint64 ReadSize = FMath::Min(ResolvedRequest.ResolvedFileSize, BlockOffset + CacheBlockSize) - BlockOffset;

		CachedBlock = new FFileIoStoreReadBlock();
		CachedBlock->Key = Key;
		CachedBlock->bIsReady = false;
		CachedBlock->Offset = BlockOffset;
		CachedBlock->Size = ReadSize;
		CachedBlocksMap.Add(CachedBlock->Key, CachedBlock);

		PlatformImpl.ReadBlockFromFile(CachedBlock);
		TRACE_COUNTER_INCREMENT(IoDispatcherCacheMisses);
	}
	else
	{
		if (CachedBlock->bIsReady)
		{
			TRACE_COUNTER_INCREMENT(IoDispatcherCacheHitsHot);
		}
		else
		{
			TRACE_COUNTER_INCREMENT(IoDispatcherCacheHitsCold);
		}
	}

	if (CachedBlock->LruPrev)
	{
		check(CachedBlock->LruNext);
		CachedBlock->LruPrev->LruNext = CachedBlock->LruNext;
		CachedBlock->LruNext->LruPrev = CachedBlock->LruPrev;
	}

	CachedBlock->LruNext = LruHead.LruNext;
	CachedBlock->LruPrev = &LruHead;
	LruHead.LruNext->LruPrev = CachedBlock;
	LruHead.LruNext = CachedBlock;

	uint64 RequestStartOffsetInBlock = FMath::Max<int64>(0, int64(ResolvedRequest.ResolvedOffset) - BlockOffset);
	uint64 RequestEndOffsetInBlock = FMath::Min<uint64>(CacheBlockSize, ResolvedRequest.ResolvedOffset + ResolvedRequest.ResolvedSize - BlockOffset);
	uint64 BlockOffsetInRequest = FMath::Max<int64>(0, int64(BlockOffset) - ResolvedRequest.ResolvedOffset);
	uint64 RequestSizeInBlock = RequestEndOffsetInBlock - RequestStartOffsetInBlock;
	check(RequestSizeInBlock <= ResolvedRequest.Request->IoBuffer.DataSize());
	check(RequestStartOffsetInBlock + RequestSizeInBlock <= CacheBlockSize);
	check(ResolvedRequest.Request->IoBuffer.Data() + BlockOffsetInRequest + RequestSizeInBlock <= ResolvedRequest.Request->IoBuffer.Data() + ResolvedRequest.Request->IoBuffer.DataSize());
	if (CachedBlock->bIsReady)
	{
		FMemory::Memcpy(ResolvedRequest.Request->IoBuffer.Data() + BlockOffsetInRequest, CachedBlock->Buffer.Data() + RequestStartOffsetInBlock, RequestSizeInBlock);
		TRACE_COUNTER_ADD(IoDispatcherTotalBytesScattered, RequestSizeInBlock);
	}
	else
	{
		++ResolvedRequest.Request->UnfinishedReadsCount;
		FFileIoStoreReadBlockScatter& Scatter = CachedBlock->ScatterList.AddDefaulted_GetRef();
		Scatter.Request = ResolvedRequest.Request;
		Scatter.DstOffset = BlockOffsetInRequest;
		Scatter.SrcOffset = RequestStartOffsetInBlock;
		check(RequestSizeInBlock <= TNumericLimits<uint32>::Max());
		Scatter.Size = (uint32)RequestSizeInBlock;
	}
}

void FFileIoStore::ReadBlocksUncached(uint32 BeginBlockIndex, uint32 BlockCount, FFileIoStoreResolvedRequest& ResolvedRequest)
{
	uint64 BlockOffset = uint64(BeginBlockIndex) * uint64(CacheBlockSize);
	uint64 BlockOffsetInRequest = BlockOffset - ResolvedRequest.ResolvedOffset;
	uint64 ReadSize = BlockCount * CacheBlockSize;
	check(ResolvedRequest.Request->IoBuffer.Data() + BlockOffsetInRequest + ReadSize <= ResolvedRequest.Request->IoBuffer.Data() + ResolvedRequest.Request->IoBuffer.DataSize());

	FFileIoStoreReadBlock* UncachedBlock = new FFileIoStoreReadBlock();
	UncachedBlock->Size = ReadSize;
	UncachedBlock->Offset = BlockOffset;
	UncachedBlock->Key.FileHandle = ResolvedRequest.ResolvedFileHandle;
	UncachedBlock->Key.BlockIndex = BlockOffset / CacheBlockSize;
	UncachedBlock->Buffer = FIoBuffer(FIoBuffer::Wrap, ResolvedRequest.Request->IoBuffer.Data() + BlockOffsetInRequest, ReadSize);

	++ResolvedRequest.Request->UnfinishedReadsCount;
	FFileIoStoreReadBlockScatter& Scatter = UncachedBlock->ScatterList.AddDefaulted_GetRef();
	Scatter.Request = ResolvedRequest.Request;
	Scatter.DstOffset = MAX_uint64;
	Scatter.SrcOffset = MAX_uint64;
	Scatter.Size = ReadSize;
	PlatformImpl.ReadBlockFromFile(UncachedBlock);
}
