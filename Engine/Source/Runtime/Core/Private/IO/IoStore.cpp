// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/IoStore.h"
#include "Containers/Map.h"
#include "HAL/FileManager.h"
#include "Templates/UniquePtr.h"
#include "Misc/Paths.h"
#include "Misc/Compression.h"
#include "Serialization/BufferWriter.h"
#include "Async/Async.h"
#include "Async/Future.h"
#include "Async/ParallelFor.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/StringBuilder.h"

//////////////////////////////////////////////////////////////////////////

constexpr char FIoStoreTocHeader::TocMagicImg[];

//////////////////////////////////////////////////////////////////////////

/**
 * I/O store compresssion info.
 */
struct FIoStoreCompressionInfo
{
	enum
	{
		/** Compression method name max length. */
		CompressionMethodNameLen = 32
	};

	uint8 GetCompressionMethodIndex(FName CompressionMethod)
	{
		if (CompressionMethod == NAME_None)
		{
			return 0;
		}

		for (int32 Idx = 0; Idx < CompressionMethods.Num(); ++Idx)
		{
			if (CompressionMethods[Idx] == CompressionMethod)
			{
				return uint8(Idx) + 1;
			}
		}

		const uint8 Idx = uint8(CompressionMethods.Num());
		CompressionMethods.Add(CompressionMethod);

		return Idx + 1;
	}

	TArray<FIoStoreTocCompressedBlockEntry> BlockEntries;
	TArray<FName> CompressionMethods;
	int64 BlockSize = 0;
	int64 UncompressedContainerSize = 0;
	int64 CompressedContainerSize = 0;
};

FIoStoreEnvironment::FIoStoreEnvironment()
{
}

FIoStoreEnvironment::~FIoStoreEnvironment()
{
}

void FIoStoreEnvironment::InitializeFileEnvironment(FStringView InPath, int32 InOrder)
{
	Path = InPath;
	Order = InOrder;
}

//////////////////////////////////////////////////////////////////////////

struct FIoStoreWriterBlock
{
	FIoStoreWriterBlock* Next = nullptr;
	FName CompressionMethod = NAME_None;
	uint64 CompressedSize = 0;
	uint64 Alignment = 0;
	FIoBuffer UncompressedData;
	FIoBuffer CompressedData;
	FGraphEventRef CompressionTask;
	bool bForceUncompressed = false;
};

class FIoStoreWriterContextImpl
{
public:
	FIoStoreWriterContextImpl()
	{

	}

	~FIoStoreWriterContextImpl()
	{
		if (FreeBlockEvent)
		{
			FPlatformProcess::ReturnSynchEventToPool(FreeBlockEvent);
		}
	}

	UE_NODISCARD FIoStatus Initialize(const FIoStoreWriterSettings& InWriterSettings)
	{
		check(InWriterSettings.CompressionBlockSize > 0);
		WriterSettings = InWriterSettings;
		FreeBlockEvent = FPlatformProcess::GetSynchEventFromPool(false);
		int32 BlockCount = int32((2ull << 30) / WriterSettings.CompressionBlockSize);
		Blocks.SetNum(BlockCount);
		for (int32 BlockIndex = 0; BlockIndex < BlockCount; ++BlockIndex)
		{
			FIoStoreWriterBlock& Block = Blocks[BlockIndex];
			Block.UncompressedData = FIoBuffer(WriterSettings.CompressionBlockSize);
			int32 MaxCompressedSize = FCompression::CompressMemoryBound(WriterSettings.CompressionMethod, int32(WriterSettings.CompressionBlockSize));
			Block.CompressedData = FIoBuffer(MaxCompressedSize);
			Block.Next = FirstFreeBlock;
			FirstFreeBlock = &Block;
		}

		return FIoStatus::Ok;
	}

	const FIoStoreWriterSettings& Settings() const
	{
		return WriterSettings;
	}

	FIoStoreWriterBlock* AllocBlock()
	{
		for (;;)
		{
			{
				FScopeLock Lock(&FreeBlocksCritical);
				if (FirstFreeBlock)
				{
					FIoStoreWriterBlock* Result = FirstFreeBlock;
					FirstFreeBlock = FirstFreeBlock->Next;
					Result->bForceUncompressed = false;
					Result->Alignment = WriterSettings.CompressionBlockAlignment;
					return Result;
				}
			}
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(WaitForBlock);
				FreeBlockEvent->Wait();
			}
		}
	}

	void FreeBlock(FIoStoreWriterBlock* Block)
	{
		{
			FScopeLock Lock(&FreeBlocksCritical);
			Block->Next = FirstFreeBlock;
			FirstFreeBlock = Block;
		}
		FreeBlockEvent->Trigger();
	}

private:
	FIoStoreWriterSettings WriterSettings;
	TArray<FIoStoreWriterBlock> Blocks;
	FCriticalSection FreeBlocksCritical;
	FIoStoreWriterBlock* FirstFreeBlock = nullptr;
	FEvent* FreeBlockEvent = nullptr;
};

FIoStoreWriterContext::FIoStoreWriterContext()
	: Impl(new FIoStoreWriterContextImpl())
{

}

FIoStoreWriterContext::~FIoStoreWriterContext()
{
	delete Impl;
}

UE_NODISCARD FIoStatus FIoStoreWriterContext::Initialize(const FIoStoreWriterSettings& InWriterSettings)
{
	return Impl->Initialize(InWriterSettings);
}

static uint64 GetPadding(const uint64 Offset, const uint64 Alignment)
{
	return (Alignment - (Offset % Alignment)) % Alignment;
}

class FChunkWriter
{
public:
	FChunkWriter() { }

	virtual ~FChunkWriter()
	{
		if (WriteQueueEvent)
		{
			FPlatformProcess::ReturnSynchEventToPool(WriteQueueEvent);
		}
	}

	FIoStatus Initialize(FIoStoreWriterContextImpl& InContext, const TCHAR* InFileName, bool bInIsContainerCompressed)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(InitializeChunkWriter);
		WriterContext = &InContext;
		bIsContainerCompressed = bInIsContainerCompressed;
		check(WriterContext->Settings().CompressionBlockSize > 0);
		CompressionInfo.BlockSize = WriterContext->Settings().CompressionBlockSize;

		IPlatformFile& Ipf = IPlatformFile::GetPlatformPhysical();
		FileHandle.Reset(Ipf.OpenWrite(InFileName, /* append */ false, /* allowread */ true));
		if (!FileHandle)
		{
			return FIoStatus(EIoErrorCode::FileNotOpen, TEXT("Failed to open container file handle"));
		}

		WriteQueueEvent = FPlatformProcess::GetSynchEventFromPool(false);
		WriterTask = Async(EAsyncExecution::Thread, [this]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(WriteContainerThread);
			for (;;)
			{
				FIoStoreWriterBlock* NextWriteQueueItem = nullptr;
				{
					FScopeLock Lock(&WriteQueueCritical);
					NextWriteQueueItem = WriteQueueHead;
					WriteQueueHead = WriteQueueTail = nullptr;
				}
				if (!NextWriteQueueItem && bAllScheduled)
				{
					return;
				}
				while (NextWriteQueueItem)
				{
					FIoStoreWriterBlock* CurrentWriteQueueItem = NextWriteQueueItem;
					NextWriteQueueItem = NextWriteQueueItem->Next;

					if (bIsContainerCompressed && !CurrentWriteQueueItem->bForceUncompressed)
					{
						FTaskGraphInterface::Get().WaitUntilTaskCompletes(CurrentWriteQueueItem->CompressionTask);
					}
					if (CurrentWriteQueueItem->Alignment > 0)
					{
						const uint64 Padding = GetPadding(FileHandle->Tell(), CurrentWriteQueueItem->Alignment);
						if (Padding > 0)
						{
							AlignmentPaddingBuffer.SetNumZeroed(int32(Padding), false);
							FileHandle->Write(AlignmentPaddingBuffer.GetData(), Padding);
							PaddingSize += Padding;
						}
						check(FileHandle->Tell() % CurrentWriteQueueItem->Alignment == 0);
					}
					const int64 CompressedFileOffset = FileHandle->Tell();
					FIoStoreTocCompressedBlockEntry& CompressedBlockEntry = CompressionInfo.BlockEntries.AddDefaulted_GetRef();
					CompressedBlockEntry.OffsetAndLength.SetOffset(CompressedFileOffset);
					const uint8* SourceData;
					uint64 SourceDataSize;
					if (CurrentWriteQueueItem->CompressionMethod == NAME_None)
					{
						SourceData = CurrentWriteQueueItem->UncompressedData.Data();
						SourceDataSize = CurrentWriteQueueItem->UncompressedData.DataSize();
					}
					else
					{
						SourceData = CurrentWriteQueueItem->CompressedData.Data();
						SourceDataSize = CurrentWriteQueueItem->CompressedSize;
					}

					CompressedBlockEntry.OffsetAndLength.SetLength(SourceDataSize);
					CompressedBlockEntry.CompressionMethodIndex = CompressionInfo.GetCompressionMethodIndex(CurrentWriteQueueItem->CompressionMethod);
					{
						TRACE_CPUPROFILER_EVENT_SCOPE(WriteBlockToFile);
						FileHandle->Write(SourceData, SourceDataSize);
					}
					WriterContext->FreeBlock(CurrentWriteQueueItem);
				}
				if (!bAllScheduled)
				{
					WriteQueueEvent->Wait();
				}
			}
		});
		return FIoStatus::Ok;
	}

	TIoStatusOr<FIoStoreTocEntry> Write(FIoChunkId ChunkId, FIoBuffer Chunk, FIoWriteOptions WriteOptions)
	{
		if (WriteOptions.Alignment > 0)
		{
			if (CurrentBlockOffset > 0)
			{
				FIoStatus IoStatus = FIoStatusBuilder(EIoErrorCode::InvalidParameter) << TEXT("Can only force alignment at the start of a block");
				return IoStatus;
			}
			if (!CurrentBlock)
			{
				CurrentBlock = WriterContext->AllocBlock();
			}
			CurrentBlock->Alignment = WriteOptions.Alignment;
		}

		if (bIsContainerCompressed && WriteOptions.bForceUncompressed)
		{
			if (CurrentBlockOffset > 0 && CurrentBlock && !CurrentBlock->bForceUncompressed)
			{
				FIoStatus IoStatus = FIoStatusBuilder(EIoErrorCode::InvalidParameter) << TEXT("Can only change compression mode at the start of a block");
				return IoStatus;
			}
		}

		FIoStoreTocEntry TocEntry;
		TocEntry.SetOffset(UncompressedOffset);
		TocEntry.SetLength(Chunk.DataSize());
		TocEntry.ChunkId = ChunkId;

		UncompressedOffset += Chunk.DataSize();

		uint64 RemainingBytesInChunk = Chunk.DataSize();
		uint64 OffsetInChunk = 0;
		while (RemainingBytesInChunk > 0)
		{
			if (!CurrentBlock)
			{
				CurrentBlock = WriterContext->AllocBlock();
				check(CurrentBlock);
				if (RemainingBytesInChunk >= CurrentBlock->UncompressedData.DataSize())
				{
					// Entire block belongs to this chunk, skip patch alignment
					CurrentBlock->Alignment = 0;
				}
			}
			check(CurrentBlock->UncompressedData.DataSize() > CurrentBlockOffset);
			CurrentBlock->bForceUncompressed = WriteOptions.bForceUncompressed;
			uint64 BytesToWrite = FMath::Min(RemainingBytesInChunk, CurrentBlock->UncompressedData.DataSize() - CurrentBlockOffset);
			FMemory::Memcpy(CurrentBlock->UncompressedData.Data() + CurrentBlockOffset, Chunk.Data() + OffsetInChunk, BytesToWrite);
			OffsetInChunk += BytesToWrite;
			check(RemainingBytesInChunk >= BytesToWrite);
			RemainingBytesInChunk -= BytesToWrite;
			CurrentBlockOffset += BytesToWrite;
			if (CurrentBlockOffset == CurrentBlock->UncompressedData.DataSize())
			{
				FlushCurrentBlock();
			}
		}
		
		return TocEntry;
	}

	FIoStatus WritePadding(uint64 Count)
	{
		while (Count > 0)
		{
			if (!CurrentBlock)
			{
				CurrentBlock = WriterContext->AllocBlock();
			}
			check(CurrentBlock && CurrentBlock->UncompressedData.DataSize() > CurrentBlockOffset);
			uint64 BytesToWrite = FMath::Min(Count, CurrentBlock->UncompressedData.DataSize() - CurrentBlockOffset);
			FMemory::Memzero(CurrentBlock->UncompressedData.Data() + CurrentBlockOffset, BytesToWrite);
			check(Count >= BytesToWrite);
			Count -= BytesToWrite;
			CurrentBlockOffset += BytesToWrite;
			UncompressedOffset += BytesToWrite;
			if (CurrentBlockOffset == CurrentBlock->UncompressedData.DataSize())
			{
				FlushCurrentBlock();
			}
		}
		return FIoStatus::Ok;
	}

	FIoStatus Flush()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(EndWriteContainer);
		FlushCurrentBlock();
		bAllScheduled = true;
		WriteQueueEvent->Trigger();
		WriterTask.Wait();
		
		check(!WriteQueueHead);
		check(!WriteQueueTail);
		CompressionInfo.UncompressedContainerSize = UncompressedOffset;
		CompressionInfo.CompressedContainerSize = FileHandle.IsValid() ? FileHandle->Tell() : 0;

		return FIoStatus::Ok;
	}

	const FIoStoreCompressionInfo& GetCompressionInfo()
	{
		return CompressionInfo;
	}

	int64 GetPaddingSize()
	{
		return PaddingSize;
	}

private:
	void CompressBlock(FIoStoreWriterBlock* Block)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CompressBlock);
		int32 CompressedSize = int32(Block->CompressedData.DataSize());
		const bool bCompressed = FCompression::CompressMemory(
			WriterContext->Settings().CompressionMethod,
			Block->CompressedData.Data(),
			CompressedSize,
			Block->UncompressedData.Data(),
			int32(Block->UncompressedData.DataSize()));
		check(bCompressed);
		check(CompressedSize > 0);

		if (CompressedSize > Block->UncompressedData.DataSize())
		{
			Block->CompressionMethod = NAME_None;
			Block->CompressedSize = Block->UncompressedData.DataSize();
		}
		else
		{
			Block->CompressionMethod = WriterContext->Settings().CompressionMethod;
			Block->CompressedSize = CompressedSize;
		}
	}

	void FlushCurrentBlock()
	{
		if (!CurrentBlock)
		{
			return;
		}
		if (CurrentBlockOffset < CurrentBlock->UncompressedData.DataSize())
		{
			uint64 PadCount = CurrentBlock->UncompressedData.DataSize() - CurrentBlockOffset;
			FMemory::Memzero(CurrentBlock->UncompressedData.Data() + CurrentBlockOffset, PadCount);
			UncompressedOffset += PadCount;
		}
		if (bIsContainerCompressed && !CurrentBlock->bForceUncompressed)
		{
			FIoStoreWriterBlock* ReadyToCompressBlock = CurrentBlock;
			CurrentBlock->CompressionTask = FFunctionGraphTask::CreateAndDispatchWhenReady([this, ReadyToCompressBlock]()
			{
				CompressBlock(ReadyToCompressBlock);
			}, TStatId(), nullptr, ENamedThreads::AnyHiPriThreadHiPriTask);
		}
		else
		{
			CurrentBlock->CompressionMethod = NAME_None;
			CurrentBlock->CompressedSize = CurrentBlock->UncompressedData.DataSize();
		}
		{
			FScopeLock Lock(&WriteQueueCritical);
			if (!WriteQueueTail)
			{
				WriteQueueHead = WriteQueueTail = CurrentBlock;
			}
			else
			{
				WriteQueueTail->Next = CurrentBlock;
				WriteQueueTail = CurrentBlock;
			}
			CurrentBlock->Next = nullptr;
		}
		WriteQueueEvent->Trigger();
		CurrentBlock = nullptr;
		CurrentBlockOffset = 0;
	}

	FIoStoreWriterContextImpl* WriterContext = nullptr;
	FIoStoreCompressionInfo CompressionInfo;
	int64 PaddingSize = 0;
	TUniquePtr<IFileHandle> FileHandle;
	uint64 UncompressedOffset = 0;
	FIoStoreWriterBlock* CurrentBlock = nullptr;
	uint64 CurrentBlockOffset = 0;
	TFuture<void> WriterTask;
	FCriticalSection WriteQueueCritical;
	FIoStoreWriterBlock* WriteQueueHead = nullptr;
	FIoStoreWriterBlock* WriteQueueTail = nullptr;
	FEvent* WriteQueueEvent = nullptr;
	TAtomic<bool> bAllScheduled{ false };
	TArray<uint8> AlignmentPaddingBuffer;
	bool bIsContainerCompressed = false;
};

//////////////////////////////////////////////////////////////////////////

class FIoStoreWriterImpl
{
public:
	FIoStoreWriterImpl(FIoStoreEnvironment& InEnvironment, FIoContainerId InContainerId)
		: Environment(InEnvironment)
		, ContainerId(InContainerId)
	{
	}

	UE_NODISCARD FIoStatus Initialize(FIoStoreWriterContextImpl& InContext, bool bInIsContainerCompressed)
	{
		IPlatformFile& Ipf = IPlatformFile::GetPlatformPhysical();

		FString TocFilePath = Environment.GetPath() + TEXT(".utoc");
		FString ContainerFilePath = Environment.GetPath() + TEXT(".ucas");

		Result.ContainerId = ContainerId;
		Result.ContainerName = FPaths::GetBaseFilename(Environment.GetPath());
		Result.CompressionMethod = InContext.Settings().CompressionMethod;

		Ipf.CreateDirectoryTree(*FPaths::GetPath(ContainerFilePath));

		ChunkWriter = MakeUnique<FChunkWriter>();
		FIoStatus Status = ChunkWriter->Initialize(InContext, *ContainerFilePath, bInIsContainerCompressed);

		if (!Status.IsOk())
		{ 
			ChunkWriter.Reset();

			return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to open IoStore container file '") << *ContainerFilePath << TEXT("'");
		}

		TocFileHandle.Reset(Ipf.OpenWrite(*TocFilePath, /* append */ false, /* allowread */ true));

		if (!TocFileHandle)
		{
			return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to open IoStore TOC file '") << *TocFilePath << TEXT("'");
		}

		if (InContext.Settings().bEnableCsvOutput)
		{
			Status = EnableCsvOutput();
		}

		return Status;
	}

	FIoStatus EnableCsvOutput()
	{
		FString CsvFilePath = Environment.GetPath() + TEXT(".csv");
		CsvArchive.Reset(IFileManager::Get().CreateFileWriter(*CsvFilePath));
		if (!CsvArchive)
		{
			return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to open IoStore CSV file '") << *CsvFilePath << TEXT("'");
		}
		ANSICHAR Header[] = "Name,Offset,Size\n";
		CsvArchive->Serialize(Header, sizeof(Header) - 1);

		return FIoStatus::Ok;
	}

	UE_NODISCARD FIoStatus Append(const FIoChunkId& ChunkId, FIoBuffer Chunk, const FIoWriteOptions& WriteOptions)
	{
		return Append(ChunkId, FIoChunkHash::HashBuffer(Chunk.Data(), Chunk.DataSize()), Chunk, WriteOptions);
	}

	UE_NODISCARD FIoStatus Append(const FIoChunkId& ChunkId, const FIoChunkHash& ChunkHash, FIoBuffer Chunk, const FIoWriteOptions& WriteOptions)
	{
		if (!ChunkWriter)
		{
			return FIoStatus(EIoErrorCode::FileNotOpen, TEXT("No container file to append to"));
		}

		if (!ChunkId.IsValid())
		{
			return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("ChunkId is not valid!"));
		}

		if (Toc.Find(ChunkId) != nullptr)
		{
			return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("ChunkId is already mapped"));
		}

		IsMetadataDirty = true;

		TIoStatusOr<FIoStoreTocEntry> TocEntryStatus = ChunkWriter->Write(ChunkId, Chunk, WriteOptions);

		if (TocEntryStatus.IsOk())
		{
			FIoStoreTocEntry TocEntry = TocEntryStatus.ConsumeValueOrDie();
			TocEntry.ChunkHash = ChunkHash;
			Toc.Add(ChunkId, TocEntry);

			if (CsvArchive)
			{
				ANSICHAR Line[MAX_SPRINTF];
				FCStringAnsi::Sprintf(Line, "%s,%lld,%lld\n", (WriteOptions.DebugName ? TCHAR_TO_ANSI(WriteOptions.DebugName) : ""), TocEntry.GetOffset(), TocEntry.GetLength());
				CsvArchive->Serialize(Line, FCStringAnsi::Strlen(Line));
			}

			return FIoStatus::Ok;
		}
		else
		{
			return FIoStatus(EIoErrorCode::WriteError, TEXT("Append failed"));
		}
	}

	UE_NODISCARD FIoStatus AppendPadding(uint64 Count)
	{
		return ChunkWriter->WritePadding(Count);
	}

	UE_NODISCARD FIoStatus MapPartialRange(FIoChunkId OriginalChunkId, uint64 Offset, uint64 Length, FIoChunkId ChunkIdPartialRange)
	{
		//TODO: Does RelativeOffset + Length overflow?

		const FIoStoreTocEntry* Entry = Toc.Find(OriginalChunkId);
		if (Entry == nullptr)
		{
			return FIoStatus(EIoErrorCode::UnknownChunkID, TEXT("OriginalChunkId does not exist in the container"));
		}

		if (!ChunkIdPartialRange.IsValid())
		{
			return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("ChunkIdPartialRange is not valid!"));
		}

		if (Toc.Find(ChunkIdPartialRange) != nullptr)
		{
			return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("ChunkIdPartialRange is already mapped"));
		}

		if (Offset + Length > Entry->GetLength())
		{
			return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("The given range (Offset/Length) is not within the bounds of OriginalChunkId's data"));
		}

		FIoStoreTocPartialEntry TocPartialEntry;

		TocPartialEntry.SetOffset(Entry->GetOffset() + Offset);
		TocPartialEntry.SetLength(Length);
		TocPartialEntry.ChunkId = ChunkIdPartialRange;
		TocPartialEntry.OriginalChunkId = OriginalChunkId;

		PartialToc.Add(ChunkIdPartialRange, TocPartialEntry);

		IsMetadataDirty = true;

		return FIoStatus::Ok;
	}

	UE_NODISCARD TIoStatusOr<FIoStoreWriterResult> Flush()
	{
		if (!IsMetadataDirty)
		{
			return Result;
		}

		IsMetadataDirty = false;

		ChunkWriter->Flush();
		const FIoStoreCompressionInfo& CompressionInfo = ChunkWriter->GetCompressionInfo();

		FIoStoreTocHeader TocHeader;
		FMemory::Memzero(&TocHeader, sizeof(TocHeader));

		TocHeader.MakeMagic();
		TocHeader.TocHeaderSize = sizeof(TocHeader);
		TocHeader.TocEntryCount = Toc.Num();
		TocHeader.TocEntrySize = sizeof(FIoStoreTocEntry);
		TocHeader.TocPartialEntryCount = PartialToc.Num();
		TocHeader.TocPartialEntrySize = sizeof(FIoStoreTocPartialEntry);
		TocHeader.TocCompressedBlockEntryCount = CompressionInfo.BlockEntries.Num();
		TocHeader.TocCompressedBlockEntrySize = sizeof(FIoStoreTocCompressedBlockEntry);
		TocHeader.CompressionBlockSize = uint32(CompressionInfo.BlockSize);
		TocHeader.CompressionMethodNameCount = CompressionInfo.CompressionMethods.Num();
		TocHeader.CompressionMethodNameLength = FIoStoreCompressionInfo::CompressionMethodNameLen;
		TocHeader.ContainerId = ContainerId;

		TocFileHandle->Seek(0);
		if (!TocFileHandle->Write(reinterpret_cast<const uint8*>(&TocHeader), sizeof(TocHeader)))
		{
			return FIoStatus(EIoErrorCode::WriteError, TEXT("Failed to write TOC header"));
		}

		// Chunk entries
		for (auto& _: Toc)
		{
			FIoStoreTocEntry& TocEntry = _.Value;
			
			if (!TocFileHandle->Write(reinterpret_cast<const uint8*>(&TocEntry), sizeof(TocEntry)))
			{
				return FIoStatus(EIoErrorCode::WriteError, TEXT("Failed to write TOC entry"));
			}
		}

		for (auto& _: PartialToc)
		{
			FIoStoreTocPartialEntry& TocEntry = _.Value;

			if (!TocFileHandle->Write(reinterpret_cast<const uint8*>(&TocEntry), sizeof(TocEntry)))
			{
				return FIoStatus(EIoErrorCode::WriteError, TEXT("Failed to write TOC entry"));
			}
		}

		// Compression blocks
		for (const FIoStoreTocCompressedBlockEntry& CompressedBlockEntry : CompressionInfo.BlockEntries)
		{
			if (!TocFileHandle->Write(reinterpret_cast<const uint8*>(&CompressedBlockEntry), sizeof(CompressedBlockEntry)))
			{
				return FIoStatus(EIoErrorCode::WriteError, TEXT("Failed to write compression block TOC entry"));
			}
		}

		// Compression methods
		ANSICHAR AnsiMethodName[FIoStoreCompressionInfo::CompressionMethodNameLen];

		for (FName MethodName : CompressionInfo.CompressionMethods)
		{
			FMemory::Memzero(AnsiMethodName, FIoStoreCompressionInfo::CompressionMethodNameLen);
			FCStringAnsi::Strcpy(AnsiMethodName, FIoStoreCompressionInfo::CompressionMethodNameLen, TCHAR_TO_ANSI(*MethodName.ToString()));

			if (!TocFileHandle->Write(reinterpret_cast<const uint8*>(AnsiMethodName), FIoStoreCompressionInfo::CompressionMethodNameLen))
			{
				return FIoStatus(EIoErrorCode::WriteError, TEXT("Failed to write compression method TOC entry"));
			}
		}

		Result.TocSize = TocFileHandle->Tell();
		Result.TocEntryCount = TocHeader.TocEntryCount + TocHeader.TocPartialEntryCount;
		Result.PaddingSize = ChunkWriter->GetPaddingSize();
		Result.UncompressedContainerSize = CompressionInfo.UncompressedContainerSize;
		Result.CompressedContainerSize = CompressionInfo.CompressedContainerSize;

		return Result;
	}

private:
	friend class FIoStoreWriter;
	FIoStoreEnvironment&				Environment;
	TMap<FIoChunkId, FIoStoreTocEntry>	Toc;
	TMap<FIoChunkId, FIoStoreTocPartialEntry> PartialToc;
	TUniquePtr<FChunkWriter>			ChunkWriter;
	TUniquePtr<IFileHandle>				TocFileHandle;
	TUniquePtr<FArchive>				CsvArchive;
	FIoStoreWriterResult				Result;
	FIoContainerId						ContainerId;
	bool								IsMetadataDirty = true;
};

FIoStoreWriter::FIoStoreWriter(FIoStoreEnvironment& InEnvironment, FIoContainerId InContainerId)
:	Impl(new FIoStoreWriterImpl(InEnvironment, InContainerId))
{
}

FIoStoreWriter::~FIoStoreWriter()
{
	(void)Impl->Flush();
}

FIoStatus FIoStoreWriter::Initialize(const FIoStoreWriterContext& Context, bool bIsContainerCompressed)
{
	return Impl->Initialize(*Context.Impl, bIsContainerCompressed);
}

FIoStatus FIoStoreWriter::Append(const FIoChunkId& ChunkId, FIoBuffer Chunk, const FIoWriteOptions& WriteOptions)
{
	return Impl->Append(ChunkId, Chunk, WriteOptions);
}

FIoStatus FIoStoreWriter::Append(const FIoChunkId& ChunkId, const FIoChunkHash& ChunkHash, FIoBuffer Chunk, const FIoWriteOptions& WriteOptions)
{
	return Impl->Append(ChunkId, ChunkHash, Chunk, WriteOptions);
}

FIoStatus FIoStoreWriter::AppendPadding(uint64 Count)
{
	return Impl->AppendPadding(Count);
}

FIoStatus FIoStoreWriter::MapPartialRange(FIoChunkId OriginalChunkId, uint64 Offset, uint64 Length, FIoChunkId ChunkIdPartialRange)
{
	return Impl->MapPartialRange(OriginalChunkId, Offset, Length, ChunkIdPartialRange);
}

TIoStatusOr<FIoStoreWriterResult> FIoStoreWriter::Flush()
{
	return Impl->Flush();
}

class FIoStoreReaderImpl
{
public:
	FIoStoreReaderImpl()
	{

	}

	UE_NODISCARD FIoStatus Initialize(const FIoStoreEnvironment& Environment)
	{
		IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();

		TStringBuilder<256> ContainerFilePath;
		ContainerFilePath.Append(Environment.GetPath());

		TStringBuilder<256> TocFilePath;
		TocFilePath.Append(ContainerFilePath);

		ContainerFilePath.Append(TEXT(".ucas"));
		TocFilePath.Append(TEXT(".utoc"));

		ContainerFileHandle.Reset(Ipf.OpenRead(*ContainerFilePath, /* allowwrite */ false));
		if (!ContainerFileHandle)
		{
			return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to open IoStore container file '") << *TocFilePath << TEXT("'");
		}

		FIoStoreTocReader TocReader;
		FIoStatus Status = TocReader.Initialize(*TocFilePath);
		if (!Status.IsOk())
		{
			return Status;
		}

		uint32 TocEntryCount;
		const FIoStoreTocEntry* TocEntry = TocReader.GetEntries(TocEntryCount);

		uint32 TocPartialEntryCount;
		const FIoStoreTocPartialEntry* TocPartialEntry = TocReader.GetPartialEntries(TocPartialEntryCount);

		TocEntries.Reserve(TocEntryCount);
		int32 EntryIndex = 0;
		while (TocEntryCount--)
		{
			TocEntries.Add(*TocEntry);
			TocMap.Add(TocEntry->ChunkId, EntryIndex++);
			++TocEntry;
		}
		while (TocPartialEntryCount--)
		{
			TocPartialEntries.Add(*TocPartialEntry);
			TocMap.Add(TocPartialEntry->ChunkId, EntryIndex++);
			++TocPartialEntry;
		}

		uint32 CompressionMethodNameCount;
		const FName* CompressionMethodNames = TocReader.GetCompressionMethodNames(CompressionMethodNameCount);

		CompressionBlockSize = TocReader.GetCompressionBlockSize();
		CompressedBuffer.SetNumUninitialized(int32(CompressionBlockSize));
		UncompressedBuffer.SetNumUninitialized(int32(CompressionBlockSize));

		uint32 CompressedBlockEntryCount;
		const FIoStoreTocCompressedBlockEntry* CompressedBlockEntry = TocReader.GetCompressedBlockEntries(CompressedBlockEntryCount);
		CompressionBlocks.Reserve(CompressedBlockEntryCount);

		while (CompressedBlockEntryCount--)
		{
			FCompressionBlock& CompressionBlock = CompressionBlocks.AddDefaulted_GetRef();
			CompressionBlock.OffsetAndLength = CompressedBlockEntry->OffsetAndLength;
			CompressionBlock.CompressionMethod = CompressionMethodNames[CompressedBlockEntry->CompressionMethodIndex];
			++CompressedBlockEntry;
		}

		return FIoStatus::Ok;
	}

	void EnumerateChunks(TFunction<bool(const FIoStoreTocChunkInfo&)>&& Callback) const
	{
		for (const FIoStoreTocEntry& TocEntry : TocEntries)
		{
			FIoStoreTocChunkInfo ChunkInfo;
			ChunkInfo.Id = TocEntry.ChunkId;
			ChunkInfo.Hash = TocEntry.ChunkHash;
			ChunkInfo.Offset = TocEntry.GetOffset();
			ChunkInfo.Size = TocEntry.GetLength();
			if (!Callback(ChunkInfo))
			{
				break;
			}
		}
	}

	void EnumeratePartialChunks(TFunction<bool(const FIoStoreTocPartialChunkInfo&)>&& Callback) const
	{
		for (const FIoStoreTocPartialEntry& TocPartialEntry : TocPartialEntries)
		{
			FIoStoreTocPartialChunkInfo ChunkInfo;
			ChunkInfo.Id = TocPartialEntry.ChunkId;
			ChunkInfo.OrginalId = TocPartialEntry.OriginalChunkId;
			const int32* FindOriginalEntry = TocMap.Find(TocPartialEntry.OriginalChunkId);
			check(FindOriginalEntry);
			check(*FindOriginalEntry < TocEntries.Num());
			const FIoStoreTocEntry& OriginalTocEntry = TocEntries[*FindOriginalEntry];
			ChunkInfo.RangeStart = TocPartialEntry.GetOffset() - OriginalTocEntry.GetOffset();
			ChunkInfo.Size = TocPartialEntry.GetLength();
			if (!Callback(ChunkInfo))
			{
				break;
			}
		}
	}

	TIoStatusOr<FIoBuffer> Read(const FIoChunkId& Chunk, const FIoReadOptions& Options) const
	{
		FIoOffsetAndLength OffsetAndLength;
		const int32* FindEntryIndex = TocMap.Find(Chunk);
		if (!FindEntryIndex)
		{
			return FIoStatus(EIoErrorCode::NotFound, TEXT("Unknown chunk ID"));
		}
		if (*FindEntryIndex < TocEntries.Num())
		{
			OffsetAndLength = TocEntries[*FindEntryIndex].OffsetAndLength;
		}
		else
		{
			OffsetAndLength = TocPartialEntries[*FindEntryIndex - TocEntries.Num()].OffsetAndLength;
		}

		FIoBuffer IoBuffer = FIoBuffer(OffsetAndLength.GetLength());
		int32 FirstBlockIndex = int32(OffsetAndLength.GetOffset() / CompressionBlockSize);
		int32 LastBlockIndex = int32((Align(OffsetAndLength.GetOffset() + OffsetAndLength.GetLength(), CompressionBlockSize) - 1) / CompressionBlockSize);
		uint64 OffsetInBlock = OffsetAndLength.GetOffset() % CompressionBlockSize;
		uint8* Dst = IoBuffer.Data();
		uint8* Src = nullptr;
		uint64 RemainingSize = OffsetAndLength.GetLength();
		for (int32 BlockIndex = FirstBlockIndex; BlockIndex <= LastBlockIndex; ++BlockIndex)
		{
			const FCompressionBlock& CompressionBlock = CompressionBlocks[BlockIndex];
			if (CompressedBuffer.Num() < CompressionBlock.OffsetAndLength.GetLength())
			{
				CompressedBuffer.SetNumUninitialized(int32(CompressionBlock.OffsetAndLength.GetLength()));
			}
			ContainerFileHandle->Seek(CompressionBlock.OffsetAndLength.GetOffset());
			ContainerFileHandle->Read(CompressedBuffer.GetData(), CompressionBlock.OffsetAndLength.GetLength());
			if (CompressionBlock.CompressionMethod.IsNone())
			{
				Src = CompressedBuffer.GetData();
			}
			else
			{
				bool bUncompressed = FCompression::UncompressMemory(CompressionBlock.CompressionMethod, UncompressedBuffer.GetData(), int32(CompressionBlockSize), CompressedBuffer.GetData(), int32(CompressionBlock.OffsetAndLength.GetLength()));
				if (!bUncompressed)
				{
					return FIoStatus(EIoErrorCode::CorruptToc, TEXT("Failed uncompressing block"));
				}
				Src = UncompressedBuffer.GetData();
			}
			uint64 SizeInBlock = FMath::Min(CompressionBlockSize - OffsetInBlock, RemainingSize);
			FMemory::Memcpy(Dst, Src + OffsetInBlock, SizeInBlock);
			OffsetInBlock = 0;
			RemainingSize -= SizeInBlock;
			Dst += SizeInBlock;
		}
		
		return IoBuffer;
	}

private:
	struct FCompressionBlock
	{
		FIoOffsetAndLength OffsetAndLength;
		FName CompressionMethod;
	};

	TArray<FIoStoreTocEntry> TocEntries;
	TArray<FIoStoreTocPartialEntry> TocPartialEntries;
	TMap<FIoChunkId, int32> TocMap;
	TArray<FCompressionBlock> CompressionBlocks;
	uint64 CompressionBlockSize = 0;
	TUniquePtr<IFileHandle> ContainerFileHandle;
	mutable TArray<uint8> CompressedBuffer;
	mutable TArray<uint8> UncompressedBuffer;
};


FIoStoreReader::FIoStoreReader()
	: Impl(new FIoStoreReaderImpl())
{
}

FIoStoreReader::~FIoStoreReader()
{
	delete Impl;
}

FIoStatus FIoStoreReader::Initialize(const FIoStoreEnvironment& InEnvironment)
{
	return Impl->Initialize(InEnvironment);
}

void FIoStoreReader::EnumerateChunks(TFunction<bool(const FIoStoreTocChunkInfo&)>&& Callback) const
{
	Impl->EnumerateChunks(MoveTemp(Callback));
}

void FIoStoreReader::EnumeratePartialChunks(TFunction<bool(const FIoStoreTocPartialChunkInfo&)>&& Callback) const
{
	Impl->EnumeratePartialChunks(MoveTemp(Callback));
}

TIoStatusOr<FIoBuffer> FIoStoreReader::Read(const FIoChunkId& Chunk, const FIoReadOptions& Options) const
{
	return Impl->Read(Chunk, Options);
}

FIoStatus FIoStoreTocReader::Initialize(const TCHAR* TocFilePath)
{
	bool bTocReadOk = false;

	{
		IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();
		TUniquePtr<IFileHandle>	TocFileHandle(Ipf.OpenRead(TocFilePath, /* allowwrite */ false));

		if (!TocFileHandle)
		{
			return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to open IoStore TOC file '") << TocFilePath << TEXT("'");
		}

		const int64 TocSize = TocFileHandle->Size();
		TocBuffer = MakeUnique<uint8[]>(TocSize);
		bTocReadOk = TocFileHandle->Read(TocBuffer.Get(), TocSize);
	}

	if (!bTocReadOk)
	{
		return FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("Failed to read IoStore TOC file '") << TocFilePath << TEXT("'");
	}

	Header = reinterpret_cast<const FIoStoreTocHeader*>(TocBuffer.Get());

	if (!Header->CheckMagic())
	{
		return FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("TOC header magic mismatch while reading '") << TocFilePath << TEXT("'");
	}

	if (Header->TocHeaderSize != sizeof(FIoStoreTocHeader))
	{
		return FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("TOC header size mismatch while reading '") << TocFilePath << TEXT("'");
	}

	if (Header->TocEntrySize != sizeof(FIoStoreTocEntry))
	{
		return FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("TOC entry size mismatch while reading '") << TocFilePath << TEXT("'");
	}

	if (Header->TocPartialEntrySize != sizeof(FIoStoreTocPartialEntry))
	{
		return FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("TOC partial entry size mismatch while reading '") << TocFilePath << TEXT("'");
	}

	if (Header->TocCompressedBlockEntrySize != sizeof(FIoStoreTocCompressedBlockEntry))
	{
		return FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("TOC compressed block entry size mismatch while reading '") << TocFilePath << TEXT("'");
	}

	Entries = reinterpret_cast<const FIoStoreTocEntry*>(TocBuffer.Get() + sizeof(FIoStoreTocHeader));
	EntryCount = Header->TocEntryCount;

	PartialEntries = reinterpret_cast<const FIoStoreTocPartialEntry*>(Entries + EntryCount);
	PartialEntryCount = Header->TocPartialEntryCount;

	CompressionMethodNames.Add(NAME_None);
	CompressedBlockEntries = reinterpret_cast<const FIoStoreTocCompressedBlockEntry*>(PartialEntries + PartialEntryCount);
	CompressedBlockEntryCount = Header->TocCompressedBlockEntryCount;

	const ANSICHAR* AnsiCompressionMethodNames = reinterpret_cast<const ANSICHAR*>(CompressedBlockEntries + CompressedBlockEntryCount);
	for (uint32 CompressonNameIndex = 0; CompressonNameIndex < Header->CompressionMethodNameCount; CompressonNameIndex++)
	{
		const ANSICHAR* AnsiCompressionMethodName = AnsiCompressionMethodNames + CompressonNameIndex * Header->CompressionMethodNameLength;
		CompressionMethodNames.Add(FName(AnsiCompressionMethodName));
	}

	return FIoStatus::Ok;
}