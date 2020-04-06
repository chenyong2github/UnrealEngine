// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/IoDispatcherFileBackend.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/StringBuilder.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "HAL/PlatformFilemanager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/IConsoleManager.h"
#include "Async/AsyncWork.h"
#include "Async/MappedFileHandle.h"
#include "HAL/RunnableThread.h"
#include "Misc/ScopeLock.h"

TRACE_DECLARE_MEMORY_COUNTER(IoDispatcherTotalBytesRead, TEXT("IoDispatcher/TotalBytesRead"));
TRACE_DECLARE_MEMORY_COUNTER(IoDispatcherTotalBytesScattered, TEXT("IoDispatcher/TotalBytesScattered"));

//PRAGMA_DISABLE_OPTIMIZATION

int32 GIoDispatcherBufferSizeKB = 256;
static FAutoConsoleVariableRef CVar_IoDispatcherBufferSizeKB(
	TEXT("s.IoDispatcherBufferSizeKB"),
	GIoDispatcherBufferSizeKB,
	TEXT("IoDispatcher read buffer size (in kilobytes).")
);

int32 GIoDispatcherBufferMemoryMB = 8;
static FAutoConsoleVariableRef CVar_IoDispatcherBufferMemoryMB(
	TEXT("s.IoDispatcherBufferMemoryMB"),
	GIoDispatcherBufferMemoryMB,
	TEXT("IoDispatcher buffer memory size (in megabytes).")
);

int32 GIoDispatcherDecompressionWorkerCount = 4;
static FAutoConsoleVariableRef CVar_IoDispatcherDecompressionWorkerCount(
	TEXT("s.IoDispatcherDecompressionWorkerCount"),
	GIoDispatcherDecompressionWorkerCount,
	TEXT("IoDispatcher decompression worker count.")
);

class FMappedFileProxy final : public IMappedFileHandle
{
public:
	FMappedFileProxy(IMappedFileHandle* InSharedMappedFileHandle, uint64 InSize)
		: IMappedFileHandle(InSize)
		, SharedMappedFileHandle(InSharedMappedFileHandle)
	{
		check(InSharedMappedFileHandle != nullptr);
	}

	virtual ~FMappedFileProxy() { }

	virtual IMappedFileRegion* MapRegion(int64 Offset = 0, int64 BytesToMap = MAX_int64, bool bPreloadHint = false) override
	{
		return SharedMappedFileHandle->MapRegion(Offset, BytesToMap, bPreloadHint);
	}
private:
	IMappedFileHandle* SharedMappedFileHandle;
};

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

	if (!PlatformImpl.OpenContainer(*ContainerFilePath, ContainerFile.FileHandle, ContainerFile.FileSize))
	{
		return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to open IoStore container file '") << *ContainerFilePath << TEXT("'");
	}

	ContainerFile.FilePath = ContainerFilePath;

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

	const FIoStoreTocEntry* TocEntry = reinterpret_cast<const FIoStoreTocEntry*>(TocBuffer.Get() + sizeof(FIoStoreTocHeader));
	uint32 TocEntryCount = Header->TocEntryCount;

	uint32 CompressedBlockEntryCount = Header->CompressionBlockCount;
	const FIoStoreCompressedBlockEntry* CompressedBlockEntry = CompressedBlockEntryCount > 0
		? reinterpret_cast<const FIoStoreCompressedBlockEntry*>(TocEntry + TocEntryCount)
		: nullptr;

	const ANSICHAR* CompressionMethodNames = reinterpret_cast<const ANSICHAR*>(CompressedBlockEntry + CompressedBlockEntryCount);

	Toc.Reserve(TocEntryCount);
	while (TocEntryCount--)
	{
		if (!CompressedBlockEntryCount && (TocEntry->GetOffset() + TocEntry->GetLength()) > ContainerFile.FileSize)
		{
			return FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("TOC TocEntry out of container bounds while reading '") << *TocFilePath << TEXT("'");
		}

		Toc.Add(TocEntry->ChunkId, TocEntry->OffsetAndLength);
		++TocEntry;
	}

	if (CompressedBlockEntryCount > 0)
	{
		UE_LOG(LogIoDispatcher, Display, TEXT("Loading compressed toc: %s"), *TocFilePath);
		ContainerFile.CompressionBlockSize = Header->CompressionBlockSize;
		ContainerFile.CompressionBlocks.Reserve(Header->CompressionBlockCount);

		while (CompressedBlockEntryCount--)
		{
			if (CompressedBlockEntry->OffsetAndLength.GetOffset() + CompressedBlockEntry->OffsetAndLength.GetLength() > ContainerFile.FileSize)
			{
				return FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("TOC TocCompressedBlockEntry out of container bounds while reading '") << *TocFilePath << TEXT("'");
			}

			ContainerFile.CompressionBlocks.Emplace(*CompressedBlockEntry);
			++CompressedBlockEntry;
		}
	}
	else
	{
		UE_LOG(LogIoDispatcher, Display, TEXT("Loading uncompressed toc: %s"), *TocFilePath);
	}

	for (uint32 CompressonNameIndex = 0; CompressonNameIndex < Header->CompressionNameCount; CompressonNameIndex++)
	{
		const ANSICHAR* CompressionMethodName = CompressionMethodNames + CompressonNameIndex * FIoStoreCompressionInfo::CompressionMethodNameLen;
		ContainerFile.CompressionMethods.Add(FName(CompressionMethodName));
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

const FIoOffsetAndLength* FFileIoStoreReader::Resolve(const FIoChunkId& ChunkId) const
{
	return Toc.Find(ChunkId);
}

IMappedFileHandle* FFileIoStoreReader::GetMappedContainerFileHandle()
{
	if (!ContainerFile.MappedFileHandle)
	{
		IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();
		ContainerFile.MappedFileHandle.Reset(Ipf.OpenMapped(*ContainerFile.FilePath));
	}

	check(ContainerFile.FileSize > 0);
	return new FMappedFileProxy(ContainerFile.MappedFileHandle.Get(), ContainerFile.FileSize);
}

FFileIoStore::FFileIoStore(FIoDispatcherEventQueue& InEventQueue, bool bInIsMultithreaded)
	: ReadBufferSize(GIoDispatcherBufferSizeKB > 0 ? uint64(GIoDispatcherBufferSizeKB) << 10 : 256 << 10)
	, EventQueue(InEventQueue)
	, bIsMultithreaded(bInIsMultithreaded)
	, PlatformImpl(InEventQueue, ReadBufferSize)
	, BufferAvailableEvent(FPlatformProcess::GetSynchEventFromPool())
	, PendingBlockEvent(FPlatformProcess::GetSynchEventFromPool())
{
	uint64 BufferCount = uint64(GIoDispatcherBufferMemoryMB > 0 ? uint64(GIoDispatcherBufferMemoryMB) << 20 : 32ull << 20) / ReadBufferSize;
	uint64 MemorySize = BufferCount * ReadBufferSize;
	BufferMemory = reinterpret_cast<uint8*>(FMemory::Malloc(MemorySize));
	for (uint64 BufferIndex = 0; BufferIndex < BufferCount; ++BufferIndex)
	{
		FFileIoStoreBuffer* Buffer = new FFileIoStoreBuffer();
		Buffer->Memory = BufferMemory + BufferIndex * ReadBufferSize;
		Buffer->Next = FirstFreeBuffer;
		FirstFreeBuffer = Buffer;
	}

	uint64 DecompressionContextCount = uint64(GIoDispatcherDecompressionWorkerCount > 0 ? GIoDispatcherDecompressionWorkerCount : 4);
	for (uint64 ContextIndex = 0; ContextIndex < DecompressionContextCount; ++ContextIndex)
	{
		FFileIoStoreCompressionContext* Context = new FFileIoStoreCompressionContext();
		Context->Next = FirstFreeCompressionContext;
		FirstFreeCompressionContext = Context;
	}

	Thread = FRunnableThread::Create(this, TEXT("IoService"), 0, TPri_AboveNormal);
}

FFileIoStore::~FFileIoStore()
{
	delete Thread;
	FPlatformProcess::ReturnSynchEventToPool(PendingBlockEvent);
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
	int32 ReaderIndex = 0;
	for (FFileIoStoreReader* Reader : IoStoreReaders)
	{
		if (const FIoOffsetAndLength* OffsetAndLength = Reader->Resolve(ResolvedRequest.Request->ChunkId))
		{
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

			Request->UnfinishedReadsCount = 0;
			if (ResolvedRequest.ResolvedSize > 0)
			{
				if (void* TargetVa = Request->Options.GetTargetVa())
				{
					ResolvedRequest.Request->IoBuffer = FIoBuffer(FIoBuffer::Wrap, TargetVa, ResolvedRequest.ResolvedSize);
				}
				else
				{
					ResolvedRequest.Request->IoBuffer.SetSize(ResolvedRequest.ResolvedSize);
				}

				const FFileIoStoreContainerFile& ContainerFile = Reader->GetContainerFile();
				const bool bIsCompressed = ContainerFile.CompressionBlockSize > 0;
				const uint64 BlockSize = bIsCompressed ? ContainerFile.CompressionBlockSize : ReadBufferSize;
				const uint64 RequestEndOffset = ResolvedRequest.ResolvedOffset + ResolvedRequest.ResolvedSize;
				int32 RequestBeginBlockIndex = int32(ResolvedRequest.ResolvedOffset / BlockSize);
				int32 RequestEndBlockIndex = int32((RequestEndOffset - 1) / BlockSize + 1);

				if (bIsCompressed)
				{
					for (int32 BlockIndex = RequestBeginBlockIndex; BlockIndex < RequestEndBlockIndex; ++BlockIndex)
					{
						ReadBlockAndScatter(ReaderIndex, BlockIndex, ResolvedRequest);
					}
				}
				else
				{
					int32 NumBlocks = RequestEndBlockIndex - RequestBeginBlockIndex;

					const bool bFirstBlockIsPartial = ResolvedRequest.ResolvedOffset % BlockSize != 0;
					if (bFirstBlockIsPartial)
					{
						ReadBlockAndScatter(ReaderIndex, RequestBeginBlockIndex, ResolvedRequest);
						++RequestBeginBlockIndex;
						--NumBlocks;
					}

					if (NumBlocks > 0)
					{
						const bool bLastBlockIsPartial = RequestEndOffset % BlockSize != 0;
						const int32 NumMergedBlocks = bLastBlockIsPartial ? NumBlocks - 1  : NumBlocks;
						if (NumMergedBlocks > 0)
						{
							const uint64 MergedReadOffset = RequestBeginBlockIndex * BlockSize;
							const uint64 MergedReadSize = NumMergedBlocks * BlockSize;
							ReadNoScatter(ReaderIndex, MergedReadOffset, MergedReadSize, ResolvedRequest);
						}

						if (bLastBlockIsPartial)
						{
							ReadBlockAndScatter(ReaderIndex, RequestEndBlockIndex - 1, ResolvedRequest);
						}
					}
				}
			}

			return IoStoreResolveResult_OK;
		}
		++ReaderIndex;
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

FAutoConsoleTaskPriority CPrio_IoDispatcherTaskPriority(
	TEXT("TaskGraph.TaskPriorities.IoDispatcherAsyncTasks"),
	TEXT("Task and thread priority for IoDispatcher decompression."),
	ENamedThreads::BackgroundThreadPriority, // if we have background priority task threads, then use them...
	ENamedThreads::NormalTaskPriority, // .. at normal task priority
	ENamedThreads::NormalTaskPriority // if we don't have background threads, then use normal priority threads at normal task priority instead
);

ENamedThreads::Type FFileIoStore::FDecompressAsyncTask::GetDesiredThread()
{
	return CPrio_IoDispatcherTaskPriority.Get();
}

void FFileIoStore::ScatterBlock(FFileIoStoreCompressedBlock* CompressedBlock, bool bIsAsync)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IoDispatcherScatter);
	
	FFileIoStoreCompressionContext* CompressionContext = CompressedBlock->CompressionContext;
	check(CompressionContext);
	uint8* CompressedBuffer;
	if (CompressedBlock->RawBlocksCount > 1)
	{
		check(CompressedBlock->CompressedDataBuffer);
		CompressedBuffer = CompressedBlock->CompressedDataBuffer;
	}
	else
	{
		FFileIoStoreRawBlock* RawBlock = CompressedBlock->SingleRawBlock;
		check(CompressedBlock->RawOffset >= RawBlock->Offset);
		uint64 OffsetInBuffer = CompressedBlock->RawOffset - RawBlock->Offset;
		CompressedBuffer = RawBlock->Buffer->Memory + OffsetInBuffer;
	}
	uint8* UncompressedBuffer;
	if (CompressedBlock->CompressionMethod.IsNone())
	{
		UncompressedBuffer = CompressedBuffer;
	}
	else
	{
		if (CompressionContext->UncompressedBufferSize < CompressedBlock->Size)
		{
			FMemory::Free(CompressionContext->UncompressedBuffer);
			CompressionContext->UncompressedBuffer = reinterpret_cast<uint8*>(FMemory::Malloc(CompressedBlock->Size));
			CompressionContext->UncompressedBufferSize = CompressedBlock->Size;
		}
		UncompressedBuffer = CompressionContext->UncompressedBuffer;

		bool bFailed = !FCompression::UncompressMemory(CompressedBlock->CompressionMethod, UncompressedBuffer, int32(CompressedBlock->Size), CompressedBuffer, int32(CompressedBlock->RawSize));
		check(!bFailed);
	}

	for (FFileIoStoreBlockScatter& Scatter : CompressedBlock->ScatterList)
	{
		FMemory::Memcpy(Scatter.Request->IoBuffer.Data() + Scatter.DstOffset, UncompressedBuffer + Scatter.SrcOffset, Scatter.Size);
		check(Scatter.Request->UnfinishedReadsCount > 0);
		--Scatter.Request->UnfinishedReadsCount;
	}

	if (bIsAsync)
	{
		FScopeLock Lock(&DecompressedBlocksCritical);
		CompressedBlock->Next = FirstDecompressedBlock;
		FirstDecompressedBlock = CompressedBlock;

		EventQueue.Notify();
	}
}

void FFileIoStore::AllocMemoryForRequest(FIoRequestImpl* Request)
{
	if (!Request->IoBuffer.Data())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(AllocMemoryForRequest);
		Request->IoBuffer = FIoBuffer(Request->IoBuffer.DataSize());
	}
}

void FFileIoStore::FinalizeCompressedBlock(FFileIoStoreCompressedBlock* CompressedBlock)
{
	if (CompressedBlock->RawBlocksCount > 1)
	{
		check(CompressedBlock->CompressedDataBuffer);
		FMemory::Free(CompressedBlock->CompressedDataBuffer);
	}
	else
	{
		FFileIoStoreRawBlock* RawBlock = CompressedBlock->SingleRawBlock;
		check(RawBlock->RefCount > 0);
		if (--RawBlock->RefCount == 0)
		{
			FreeBuffer(RawBlock->Buffer);
			delete RawBlock;
		}
	}
	check(CompressedBlock->CompressionContext);
	FreeCompressionContext(CompressedBlock->CompressionContext);
	for (FFileIoStoreBlockScatter& Scatter : CompressedBlock->ScatterList)
	{
		TRACE_COUNTER_ADD(IoDispatcherTotalBytesScattered, Scatter.Size);
	}
	delete CompressedBlock;
}

void FFileIoStore::ProcessCompletedBlocks()
{
	//TRACE_CPUPROFILER_EVENT_SCOPE(ProcessCompletedBlocks);
	
	FFileIoStoreRawBlock* CompletedBlock = PlatformImpl.GetCompletedBlocks();
	while (CompletedBlock)
	{
		TRACE_COUNTER_ADD(IoDispatcherTotalBytesRead, CompletedBlock->Size);
		FFileIoStoreRawBlock* NextBlock = CompletedBlock->Next;

		if (CompletedBlock->DirectToRequest)
		{
			check(CompletedBlock->DirectToRequest->UnfinishedReadsCount > 0);
			--CompletedBlock->DirectToRequest->UnfinishedReadsCount;
			TRACE_COUNTER_ADD(IoDispatcherTotalBytesScattered, CompletedBlock->Size);
		}
		else
		{
			RawBlocksMap.Remove(CompletedBlock->Key);

			//TRACE_CPUPROFILER_EVENT_SCOPE(ProcessCompletedBlock);
			for (FFileIoStoreCompressedBlock* CompressedBlock : CompletedBlock->CompressedBlocks)
			{
				if (CompressedBlock->RawBlocksCount > 1)
				{
					//TRACE_CPUPROFILER_EVENT_SCOPE(HandleComplexBlock);
					if (!CompressedBlock->CompressedDataBuffer)
					{
						CompressedBlock->CompressedDataBuffer = reinterpret_cast<uint8*>(FMemory::Malloc(CompressedBlock->RawSize));
					}

					uint8* Src = CompletedBlock->Buffer->Memory;
					uint8* Dst = CompressedBlock->CompressedDataBuffer;
					uint64 CopySize = CompletedBlock->Size;
					int64 CompletedBlockOffsetInBuffer = int64(CompletedBlock->Offset) - int64(CompressedBlock->RawOffset);
					if (CompletedBlockOffsetInBuffer < 0)
					{
						Src -= CompletedBlockOffsetInBuffer;
						CopySize += CompletedBlockOffsetInBuffer;
					}
					else
					{
						Dst += CompletedBlockOffsetInBuffer;
					}
					uint64 CompressedBlockRawEndOffset = CompressedBlock->RawOffset + CompressedBlock->RawSize;
					uint64 CompletedBlockEndOffset = CompletedBlock->Offset + CompletedBlock->Size;
					if (CompletedBlockEndOffset > CompressedBlockRawEndOffset)
					{
						CopySize -= CompletedBlockEndOffset - CompressedBlockRawEndOffset;
					}
					FMemory::Memcpy(Dst, Src, CopySize);
					check(CompletedBlock->RefCount > 0);
					--CompletedBlock->RefCount;
				}
				
				check(CompressedBlock->UnfinishedRawBlocksCount > 0);
				if (--CompressedBlock->UnfinishedRawBlocksCount == 0)
				{
					CompressedBlocksMap.Remove(CompressedBlock->Key);
					if (!ReadyForDecompressionTail)
					{
						ReadyForDecompressionHead = ReadyForDecompressionTail = CompressedBlock;
					}
					else
					{
						ReadyForDecompressionTail->Next = CompressedBlock;
						ReadyForDecompressionTail = CompressedBlock;
					}
					CompressedBlock->Next = nullptr;
				}
			}
			if (CompletedBlock->RefCount == 0)
			{
				FreeBuffer(CompletedBlock->Buffer);
				delete CompletedBlock;
			}
		}
		CompletedBlock = NextBlock;
	}
	
	FFileIoStoreCompressedBlock* BlockToReap;
	{
		FScopeLock Lock(&DecompressedBlocksCritical);
		BlockToReap = FirstDecompressedBlock;
		FirstDecompressedBlock = nullptr;
	}

	while (BlockToReap)
	{
		FFileIoStoreCompressedBlock* Next = BlockToReap->Next;
		FinalizeCompressedBlock(BlockToReap);
		BlockToReap = Next;
	}

	FFileIoStoreCompressedBlock* BlockToDecompress = ReadyForDecompressionHead;
	while (BlockToDecompress)
	{
		FFileIoStoreCompressedBlock* Next = BlockToDecompress->Next;
		BlockToDecompress->CompressionContext = AllocCompressionContext();
		if (!BlockToDecompress->CompressionContext)
		{
			break;
		}
		for (FFileIoStoreBlockScatter& Scatter : BlockToDecompress->ScatterList)
		{
			AllocMemoryForRequest(Scatter.Request);
		}
		if (BlockToDecompress->CompressionMethod.IsNone())
		{
			ScatterBlock(BlockToDecompress, false);
			FinalizeCompressedBlock(BlockToDecompress);
		}
		else
		{
			TGraphTask<FDecompressAsyncTask>::CreateTask().ConstructAndDispatchWhenReady(*this, BlockToDecompress);
		}
		BlockToDecompress = Next;
	}
	ReadyForDecompressionHead = BlockToDecompress;
	if (!ReadyForDecompressionHead)
	{
		ReadyForDecompressionTail = nullptr;
	}
}

TIoStatusOr<FIoMappedRegion> FFileIoStore::OpenMapped(const FIoChunkId& ChunkId, const FIoReadOptions& Options)
{
	if (!FPlatformProperties::SupportsMemoryMappedFiles())
	{
		return FIoStatus(EIoErrorCode::Unknown, TEXT("Platform does not support memory mapped files"));
	}

	if (Options.GetTargetVa() != nullptr)
	{
		return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("Invalid read options"));
	}

	IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();

	for (FFileIoStoreReader* Reader : IoStoreReaders)
	{
		if (const FIoOffsetAndLength* OffsetAndLength = Reader->Resolve(ChunkId))
		{
			uint64 ResolvedOffset = OffsetAndLength->GetOffset();
			uint64 ResolvedSize = FMath::Min(Options.GetSize(), OffsetAndLength->GetLength());
			
			const FFileIoStoreContainerFile& ContainerFile = Reader->GetContainerFile();
			const bool bIsCompressed = ContainerFile.CompressionBlockSize > 0;

			IMappedFileHandle* MappedFileHandle = Reader->GetMappedContainerFileHandle();
			IMappedFileRegion* MappedFileRegion = nullptr;

			if (bIsCompressed)
			{
				int32 BlockIndex = int32(ResolvedOffset / ContainerFile.CompressionBlockSize);
				const FIoStoreCompressedBlockEntry& CompressionBlockEntry = ContainerFile.CompressionBlocks[BlockIndex];
				const int64 BlockOffset = (int64)CompressionBlockEntry.OffsetAndLength.GetOffset();
				check(BlockOffset > 0 && IsAligned(BlockOffset, FPlatformProperties::GetMemoryMappingAlignment()));

				MappedFileRegion = MappedFileHandle->MapRegion(BlockOffset + Options.GetOffset(), ResolvedSize);
				check(IsAligned(MappedFileRegion->GetMappedPtr(), FPlatformProperties::GetMemoryMappingAlignment()));
			}
			else
			{
				MappedFileRegion = MappedFileHandle->MapRegion(ResolvedOffset + Options.GetOffset(), ResolvedSize);
			}

			return FIoMappedRegion { MappedFileHandle, MappedFileRegion };
		}
	}

	return FIoStatus::Invalid;
}

void FFileIoStore::ReadBlockAndScatter(uint32 ReaderIndex, uint32 BlockIndex, const FFileIoStoreResolvedRequest& ResolvedRequest)
{
	/*TStringBuilder<256> ScopeName;
	ScopeName.Appendf(TEXT("ReadBlock %d"), BlockIndex);
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*ScopeName);*/

	const FFileIoStoreContainerFile& ContainerFile = IoStoreReaders[ReaderIndex]->GetContainerFile();

	FFileIoStoreRawBlock* NewBlocksHead = nullptr;
	FFileIoStoreRawBlock* NewBlocksTail = nullptr;

	FFileIoStoreBlockKey CompressedBlockKey;
	CompressedBlockKey.FileIndex = ReaderIndex;
	CompressedBlockKey.BlockIndex = BlockIndex;
	FFileIoStoreCompressedBlock* CompressedBlock = CompressedBlocksMap.FindRef(CompressedBlockKey);
	if (!CompressedBlock)
	{
		CompressedBlock = new FFileIoStoreCompressedBlock();
		CompressedBlock->Key = CompressedBlockKey;
		CompressedBlocksMap.Add(CompressedBlockKey, CompressedBlock);

		uint64 RawOffset;
		uint64 RawSize;
		if (ContainerFile.CompressionBlockSize > 0)
		{
			const FIoStoreCompressedBlockEntry& CompressionBlockEntry = ContainerFile.CompressionBlocks[BlockIndex];
			CompressedBlock->Size = ContainerFile.CompressionBlockSize;
			CompressedBlock->CompressionMethod = 
				(CompressionBlockEntry.CompressionMethodIndex == FIoStoreCompressionInfo::InvalidCompressionIndex) ?
				NAME_None :
				ContainerFile.CompressionMethods[CompressionBlockEntry.CompressionMethodIndex];
			RawOffset = CompressionBlockEntry.OffsetAndLength.GetOffset();
			RawSize = CompressionBlockEntry.OffsetAndLength.GetLength();
		}
		else
		{
			CompressedBlock->Size = ReadBufferSize;
			RawOffset = BlockIndex * CompressedBlock->Size;
			RawSize = ReadBufferSize;
		}
		CompressedBlock->RawOffset = RawOffset;
		CompressedBlock->RawSize = RawSize;
		const uint32 RawBeginBlockIndex = uint32(RawOffset / ReadBufferSize);
		const uint32 RawEndBlockIndex = uint32((RawOffset + RawSize - 1) / ReadBufferSize + 1);
		const uint32 RawBlockCount = RawEndBlockIndex - RawBeginBlockIndex;
		CompressedBlock->RawBlocksCount = RawBlockCount;
		check(RawBlockCount > 0);
		for (uint32 RawBlockIndex = RawBeginBlockIndex; RawBlockIndex < RawEndBlockIndex; ++RawBlockIndex)
		{
			FFileIoStoreBlockKey RawBlockKey;
			RawBlockKey.BlockIndex = RawBlockIndex;
			RawBlockKey.FileIndex = ReaderIndex;

			FFileIoStoreRawBlock* RawBlock = RawBlocksMap.FindRef(RawBlockKey);
			if (!RawBlock)
			{
				RawBlock = new FFileIoStoreRawBlock();
				RawBlock->Key = RawBlockKey;
				RawBlocksMap.Add(RawBlockKey, RawBlock);

				RawBlock->Offset = RawBlockIndex * ReadBufferSize;
				uint64 ReadSize = FMath::Min(ContainerFile.FileSize, RawBlock->Offset + ReadBufferSize) - RawBlock->Offset;
				RawBlock->Size = ReadSize;
				if (!NewBlocksTail)
				{
					NewBlocksHead = NewBlocksTail = RawBlock;
				}
				else
				{
					NewBlocksTail->Next = RawBlock;
					NewBlocksTail = RawBlock;
				}
			}
			if (RawBlockCount == 1)
			{
				CompressedBlock->SingleRawBlock = RawBlock;
			}
			RawBlock->CompressedBlocks.Add(CompressedBlock);
			++RawBlock->RefCount;
			++CompressedBlock->UnfinishedRawBlocksCount;
		}
	}

	uint64 BlockOffset = BlockIndex * CompressedBlock->Size;
	uint64 RequestStartOffsetInBlock = FMath::Max<int64>(0, int64(ResolvedRequest.ResolvedOffset) - BlockOffset);
	uint64 RequestEndOffsetInBlock = FMath::Min<uint64>(CompressedBlock->Size, ResolvedRequest.ResolvedOffset + ResolvedRequest.ResolvedSize - BlockOffset);
	uint64 BlockOffsetInRequest = FMath::Max<int64>(0, int64(BlockOffset) - ResolvedRequest.ResolvedOffset);
	uint64 RequestSizeInBlock = RequestEndOffsetInBlock - RequestStartOffsetInBlock;
	check(RequestSizeInBlock <= ResolvedRequest.Request->IoBuffer.DataSize());
	check(RequestStartOffsetInBlock + RequestSizeInBlock <= CompressedBlock->Size);
	check(BlockOffsetInRequest + RequestSizeInBlock <= ResolvedRequest.Request->IoBuffer.DataSize());

	++ResolvedRequest.Request->UnfinishedReadsCount;
	FFileIoStoreBlockScatter& Scatter = CompressedBlock->ScatterList.AddDefaulted_GetRef();
	Scatter.Request = ResolvedRequest.Request;
	Scatter.DstOffset = BlockOffsetInRequest;
	Scatter.SrcOffset = RequestStartOffsetInBlock;
	Scatter.Size = RequestSizeInBlock;

	if (NewBlocksHead)
	{
		{
			FScopeLock Lock(&PendingBlocksCritical);
			if (!PendingBlocksTail)
			{
				PendingBlocksHead = NewBlocksHead;
			}
			else
			{
				PendingBlocksTail->Next = NewBlocksHead;
			}
			PendingBlocksTail = NewBlocksTail;
		}
		PendingBlockEvent->Trigger();
		if (!bIsMultithreaded)
		{
			ReadPendingBlocks();
		}
	}
}

void FFileIoStore::ReadNoScatter(uint32 ReaderIndex, uint64 Offset, uint64 Size, const FFileIoStoreResolvedRequest& ResolvedRequest)
{
	/*TStringBuilder<256> ScopeName;
	ScopeName.Appendf(TEXT("ReadNoScatter %lld %lld"), Offset, Size);
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*ScopeName);*/

	const FFileIoStoreContainerFile& ContainerFile = IoStoreReaders[ReaderIndex]->GetContainerFile();

	FFileIoStoreRawBlock* NewBlocksHead = nullptr;
	FFileIoStoreRawBlock* NewBlocksTail = nullptr;

	FFileIoStoreRawBlock* RawBlock = new FFileIoStoreRawBlock();
	RawBlock->Key.BlockIndex = -1;
	RawBlock->Key.FileIndex = ReaderIndex;
	RawBlock->Offset = Offset;
	uint64 ReadSize = FMath::Min(ContainerFile.FileSize, RawBlock->Offset + Size) - RawBlock->Offset;
	RawBlock->Size = ReadSize;
	RawBlock->DirectToRequest = ResolvedRequest.Request;
	check(Offset >= ResolvedRequest.ResolvedOffset);
	RawBlock->DirectToRequestOffset = Offset - ResolvedRequest.ResolvedOffset;
	++ResolvedRequest.Request->UnfinishedReadsCount;

	AllocMemoryForRequest(ResolvedRequest.Request);

	{
		FScopeLock Lock(&PendingBlocksCritical);
		if (!PendingBlocksTail)
		{
			PendingBlocksHead = RawBlock;
		}
		else
		{
			PendingBlocksTail->Next = RawBlock;
		}
		PendingBlocksTail = RawBlock;
	}
	PendingBlockEvent->Trigger();
	if (!bIsMultithreaded)
	{
		ReadPendingBlocks();
	}
}

FFileIoStoreBuffer* FFileIoStore::AllocBuffer()
{
	for (;;)
	{
		{
			FScopeLock Lock(&BuffersCritical);
			FFileIoStoreBuffer* Buffer = FirstFreeBuffer;
			if (Buffer)
			{
				FirstFreeBuffer = Buffer->Next;
				return Buffer;
			}
		}
		TRACE_CPUPROFILER_EVENT_SCOPE(WaitForBuffer);
		PlatformImpl.FlushReads();
		BufferAvailableEvent->Wait();
	}

	return nullptr;
}

void FFileIoStore::FreeBuffer(FFileIoStoreBuffer* Buffer)
{
	{
		FScopeLock Lock(&BuffersCritical);
		Buffer->Next = FirstFreeBuffer;
		FirstFreeBuffer = Buffer;
	}
	BufferAvailableEvent->Trigger();
}

FFileIoStoreCompressionContext* FFileIoStore::AllocCompressionContext()
{
	FFileIoStoreCompressionContext* Result = FirstFreeCompressionContext;
	if (Result)
	{
		FirstFreeCompressionContext = FirstFreeCompressionContext->Next;
	}
	return Result;
}

void FFileIoStore::FreeCompressionContext(FFileIoStoreCompressionContext* CompressionContext)
{
	CompressionContext->Next = FirstFreeCompressionContext;
	FirstFreeCompressionContext = CompressionContext;
}

void FFileIoStore::ReadPendingBlocks()
{
	while (!bStopRequested)
	{
		{
			FScopeLock PendingBlocksLock(&PendingBlocksCritical);
			if (PendingBlocksHead)
			{
				if (!ScheduledBlocksTail)
				{
					ScheduledBlocksHead = PendingBlocksHead;
					ScheduledBlocksTail = PendingBlocksTail;
				}
				else
				{
					ScheduledBlocksTail->Next = PendingBlocksHead;
					ScheduledBlocksTail = PendingBlocksTail;
				}
				PendingBlocksHead = PendingBlocksTail = nullptr;
			}
		}

		if (!ScheduledBlocksHead)
		{
			break;
		}

		FFileIoStoreRawBlock* BlockToRead = ScheduledBlocksHead;

		/*TStringBuilder<256> ScopeName;
		if (BlockToRead->DirectToRequest)
		{
			ScopeName.Appendf(TEXT("ReadNoScatter %lld %lld"), BlockToRead->Offset, BlockToRead->Size);
		}
		else
		{
			ScopeName.Appendf(TEXT("ReadBlock %d"), BlockToRead->Key.BlockIndex);
		}
		TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*ScopeName);*/

		uint8* Target;
		if (BlockToRead->DirectToRequest)
		{
			Target = BlockToRead->DirectToRequest->IoBuffer.Data() + BlockToRead->DirectToRequestOffset;
		}
		else
		{
			BlockToRead->Buffer = AllocBuffer();
			Target = BlockToRead->Buffer->Memory;
		}
		
		ScheduledBlocksHead = ScheduledBlocksHead->Next;
		if (!ScheduledBlocksHead)
		{
			ScheduledBlocksTail = nullptr;
		}
		
		PlatformImpl.ReadBlockFromFile(Target, IoStoreReaders[BlockToRead->Key.FileIndex]->GetContainerFile().FileHandle, BlockToRead);
	}
	PlatformImpl.FlushReads();
}

bool FFileIoStore::Init()
{
	return true;
}

void FFileIoStore::Stop()
{
	bStopRequested = true;
	PendingBlockEvent->Trigger();
	BufferAvailableEvent->Trigger();
}

uint32 FFileIoStore::Run()
{
	while (!bStopRequested)
	{
		ReadPendingBlocks();
		PendingBlockEvent->Wait();
	}
	return 0;
}
