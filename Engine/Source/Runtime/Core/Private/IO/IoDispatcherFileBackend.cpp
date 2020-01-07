// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/IoDispatcherFileBackend.h"
#include "Misc/ScopeRWLock.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "HAL/PlatformFilemanager.h"
#include "GenericPlatform/GenericPlatformFile.h"

TRACE_DECLARE_INT_COUNTER(IoDispatcherCacheHitsCold, TEXT("IoDispatcher/CacheHitsCold"));
TRACE_DECLARE_INT_COUNTER(IoDispatcherCacheHitsHot, TEXT("IoDispatcher/CacheHitsHot"));
TRACE_DECLARE_INT_COUNTER(IoDispatcherCacheMisses, TEXT("IoDispatcher/CacheMisses"));

FFileIoStoreReader::FFileIoStoreReader(FFileIoStoreImpl& InPlatformImpl)
	: PlatformImpl(InPlatformImpl)
{
}

FIoStatus FFileIoStoreReader::Initialize(const FIoStoreEnvironment& Environment)
{
	IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();

	TStringBuilder<256> ContainerFilePath;
	ContainerFilePath.Append(Environment.GetBasePath());
	if (ContainerFilePath.LastChar() != '/')
	{
		ContainerFilePath.Append(TEXT('/'));
	}
	if (Environment.GetPartitionName().IsEmpty())
	{
		ContainerFilePath.Append(TEXT("global"));
	}
	else
	{
		ContainerFilePath.Append(Environment.GetPartitionName());
	}
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
	uint64 FileEndOffset = OffsetAndLength->GetOffset() + OffsetAndLength->GetLength();
	uint64 RequestedBeginOffset = OffsetAndLength->GetOffset() + ResolvedRequest.Request->Options.GetOffset();
	uint64 RequestedEndOffset = FMath::Min(FileEndOffset, RequestedBeginOffset + ResolvedRequest.Request->Options.GetSize());
	ResolvedRequest.ResolvedFileSize = ContainerFileSize;
	ResolvedRequest.ResolvedOffset = RequestedBeginOffset;
	if (RequestedEndOffset > RequestedBeginOffset)
	{
		ResolvedRequest.ResolvedSize = RequestedEndOffset - RequestedBeginOffset;
	}
	else
	{
		ResolvedRequest.ResolvedSize = 0;
	}
	return true;
}

FFileIoStore::FFileIoStore(FIoDispatcherEventQueue& InEventQueue)
	: PlatformImpl(InEventQueue)
{
	InitCache();
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
			// Halt IO request submission if the two least recently used blocks are still waiting for their IO
			if (!(LruTail.LruPrev->bIsReady && LruTail.LruPrev->LruPrev->bIsReady))
			{
				return IoStoreResolveResult_Stalled;
			}

			Request->UnfinishedReadsCount = 0;
			if (ResolvedRequest.ResolvedSize > 0)
			{
				if (void* TargetVa = Request->Options.GetTargetVa())
				{
					ResolvedRequest.Request->IoBuffer = FIoBuffer(FIoBuffer::Wrap, TargetVa, ResolvedRequest.ResolvedSize);
				}
				PlatformImpl.BeginReadsForRequest(ResolvedRequest);
				uint64 RequestBeginBlockIndex = ResolvedRequest.ResolvedOffset / CacheBlockSize;
				uint64 RequestEndBlockIndex = (ResolvedRequest.ResolvedOffset + ResolvedRequest.ResolvedSize - 1) / CacheBlockSize + 1;
				uint64 BlockCount = RequestEndBlockIndex - RequestBeginBlockIndex;
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
	TocFilePath.Append(Environment.GetBasePath());
	if (TocFilePath.LastChar() != '/')
	{
		TocFilePath.Append(TEXT('/'));
	}
	if (Environment.GetPartitionName().IsEmpty())
	{
		TocFilePath.Append(TEXT("global"));
	}
	else
	{
		TocFilePath.Append(Environment.GetPartitionName());
	}
	TocFilePath.Append(TEXT(".utoc"));
	return FPlatformFileManager::Get().GetPlatformFile().FileExists(*TocFilePath);
}

void FFileIoStore::ProcessIncomingBlocks()
{
	while (FFileIoStoreReadBlock* CompletedBlock = PlatformImpl.GetNextCompletedBlock())
	{
		check(!CompletedBlock->bIsReady);
		CompletedBlock->bIsReady = true;
		for (FFileIoStoreReadBlockScatter& Scatter : CompletedBlock->ScatterList)
		{
			FMemory::Memcpy(Scatter.Dst, Scatter.Src, Scatter.Size);
			check(Scatter.Request->UnfinishedReadsCount > 0);
			--Scatter.Request->UnfinishedReadsCount;
		}
		if (CacheBlocks.GetData() <= CompletedBlock && CompletedBlock < CacheBlocks.GetData() + CacheBlockCount)
		{
			CompletedBlock->ScatterList.Empty();
		}
		else
		{
			delete CompletedBlock;
		}
	}
}

void FFileIoStore::InitCache()
{
	uint8* CacheMemoryBlock = static_cast<uint8*>(FMemory::Malloc(CacheMemorySize));
	CacheBlocks.AddDefaulted(CacheBlockCount);
	FFileIoStoreReadBlock* PreviousBlock = &LruHead;
	for (uint32 BlockIndex = 0; BlockIndex < CacheBlockCount; ++BlockIndex)
	{
		FFileIoStoreReadBlock& ReadBlock = CacheBlocks[BlockIndex];
		PreviousBlock->LruNext = &ReadBlock;
		ReadBlock.LruPrev = PreviousBlock;
		PreviousBlock = &ReadBlock;

		ReadBlock.bIsReady = true;
		ReadBlock.Buffer = CacheMemoryBlock;
		CacheMemoryBlock += CacheBlockSize;
	}
	PreviousBlock->LruNext = &LruTail;
	LruTail.LruPrev = PreviousBlock;
}

void FFileIoStore::ReadBlockCached(uint32 BlockIndex, const FFileIoStoreResolvedRequest& ResolvedRequest)
{
	FFileIoStoreCacheBlockKey Key;
	Key.FileHandle = ResolvedRequest.ResolvedFileHandle;
	Key.BlockIndex = BlockIndex;
	Key.Hash = HashCombine(ResolvedRequest.ResolvedFileHandle, BlockIndex);
	uint64 BlockOffset = uint64(BlockIndex) * uint64(CacheBlockSize);
	FFileIoStoreReadBlock* CachedBlock = CachedBlocksMap.FindRef(Key);
	if (!CachedBlock)
	{
		CachedBlock = LruTail.LruPrev;
		check(CachedBlock->bIsReady);
		CachedBlocksMap.Remove(CachedBlock->Key);
		CachedBlock->Key = Key;
		CachedBlock->bIsReady = false;
		CachedBlocksMap.Add(CachedBlock->Key, CachedBlock);

		uint64 ReadSize = FMath::Min(ResolvedRequest.ResolvedFileSize, BlockOffset + CacheBlockSize) - BlockOffset;
		PlatformImpl.ReadBlockFromFile(CachedBlock, CachedBlock->Buffer, ResolvedRequest.ResolvedFileHandle, ReadSize, BlockOffset);

		TRACE_COUNTER_INCREMENT(IoDispatcherCacheMisses);
	}

	CachedBlock->LruPrev->LruNext = CachedBlock->LruNext;
	CachedBlock->LruNext->LruPrev = CachedBlock->LruPrev;

	CachedBlock->LruNext = LruHead.LruNext;
	CachedBlock->LruPrev = &LruHead;
	CachedBlock->LruNext->LruPrev = CachedBlock;
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
		TRACE_COUNTER_INCREMENT(IoDispatcherCacheHitsHot);
		FMemory::Memcpy(ResolvedRequest.Request->IoBuffer.Data() + BlockOffsetInRequest, CachedBlock->Buffer + RequestStartOffsetInBlock, RequestSizeInBlock);
	}
	else
	{
		TRACE_COUNTER_INCREMENT(IoDispatcherCacheHitsCold);
		++ResolvedRequest.Request->UnfinishedReadsCount;
		FFileIoStoreReadBlockScatter& Scatter = CachedBlock->ScatterList.AddDefaulted_GetRef();
		Scatter.Request = ResolvedRequest.Request;
		Scatter.Dst = ResolvedRequest.Request->IoBuffer.Data() + BlockOffsetInRequest;
		Scatter.Src = CachedBlock->Buffer + RequestStartOffsetInBlock;
		Scatter.Size = RequestSizeInBlock;
	}
}

void FFileIoStore::ReadBlocksUncached(uint32 BeginBlockIndex, uint32 BlockCount, FFileIoStoreResolvedRequest& ResolvedRequest)
{
	uint64 BlockOffset = uint64(BeginBlockIndex) * uint64(CacheBlockSize);
	uint64 BlockOffsetInRequest = BlockOffset - ResolvedRequest.ResolvedOffset;
	uint64 ReadSize = BlockCount * CacheBlockSize;
	check(ResolvedRequest.Request->IoBuffer.Data() + BlockOffsetInRequest + ReadSize <= ResolvedRequest.Request->IoBuffer.Data() + ResolvedRequest.Request->IoBuffer.DataSize());

	FFileIoStoreReadBlock* UncachedBlock = new FFileIoStoreReadBlock();
	++ResolvedRequest.Request->UnfinishedReadsCount;
	FFileIoStoreReadBlockScatter& Scatter = UncachedBlock->ScatterList.AddDefaulted_GetRef();
	Scatter.Request = ResolvedRequest.Request;
	Scatter.Dst = nullptr;
	Scatter.Src = nullptr;
	Scatter.Size = 0;
	PlatformImpl.ReadBlockFromFile(UncachedBlock, ResolvedRequest.Request->IoBuffer.Data() + BlockOffsetInRequest, ResolvedRequest.ResolvedFileHandle, ReadSize, BlockOffset);
}
