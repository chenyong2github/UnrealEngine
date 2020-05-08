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
#include "Features/IModularFeatures.h"
#include "Modules/ModuleManager.h"
#include "Misc/CoreDelegates.h"

//////////////////////////////////////////////////////////////////////////

constexpr char FIoStoreTocHeader::TocMagicImg[];

//////////////////////////////////////////////////////////////////////////

static IEngineCrypto* GetEngineCrypto()
{
	static TArray<IEngineCrypto*> Features = IModularFeatures::Get().GetModularFeatureImplementations<IEngineCrypto>(IEngineCrypto::GetFeatureName());
	checkf(Features.Num() > 0, TEXT("RSA functionality was used but no modular feature was registered to provide it. Please make sure your project has the PlatformCrypto plugin enabled!"));
	return Features[0];
}

static bool IsSigningEnabled()
{
	return FCoreDelegates::GetPakSigningKeysDelegate().IsBound();
}

static FRSAKeyHandle GetPublicSigningKey()
{
	static FRSAKeyHandle PublicKey = InvalidRSAKeyHandle;
	static bool bInitializedPublicKey = false;
	if (!bInitializedPublicKey)
	{
		FCoreDelegates::FPakSigningKeysDelegate& Delegate = FCoreDelegates::GetPakSigningKeysDelegate();
		if (Delegate.IsBound())
		{
			TArray<uint8> Exponent;
			TArray<uint8> Modulus;
			Delegate.Execute(Exponent, Modulus);
			PublicKey = GetEngineCrypto()->CreateRSAKey(Exponent, TArray<uint8>(), Modulus);
		}
		bInitializedPublicKey = true;
	}

	return PublicKey;
}

static FIoStatus CreateContainerSignature(
	const FRSAKeyHandle PrivateKey,
	const FIoStoreTocHeader& TocHeader,
	TArrayView<const FSHAHash> BlockSignatureHashes,
	TArray<uint8>& OutTocSignature,
	TArray<uint8>& OutBlockSignature)
{
	if (PrivateKey == InvalidRSAKeyHandle)
	{
		return FIoStatus(EIoErrorCode::SignatureError, TEXT("Invalid signing key"));
	}

	FSHAHash TocHash, BlocksHash;

	FSHA1::HashBuffer(reinterpret_cast<const uint8*>(&TocHeader), sizeof(FIoStoreTocHeader), TocHash.Hash);
	FSHA1::HashBuffer(BlockSignatureHashes.GetData(), BlockSignatureHashes.Num() * sizeof(FSHAHash), BlocksHash.Hash);

	int32 BytesEncrypted = GetEngineCrypto()->EncryptPrivate(TArrayView<const uint8>(TocHash.Hash, UE_ARRAY_COUNT(FSHAHash::Hash)), OutTocSignature, PrivateKey);

	if (BytesEncrypted < 1)
	{
		return FIoStatus(EIoErrorCode::SignatureError, TEXT("Failed to encrypt TOC signature"));
	}

	BytesEncrypted = GetEngineCrypto()->EncryptPrivate(TArrayView<const uint8>(BlocksHash.Hash, UE_ARRAY_COUNT(FSHAHash::Hash)), OutBlockSignature, PrivateKey);

	return BytesEncrypted > 0 ? FIoStatus::Ok : FIoStatus(EIoErrorCode::SignatureError, TEXT("Failed to encrypt block signature"));
}

static FIoStatus ValidateContainerSignature(
	const FRSAKeyHandle PublicKey,
	const FIoStoreTocHeader& TocHeader,
	TArrayView<const FSHAHash> BlockSignatureHashes,
	TArrayView<const uint8> TocSignature,
	TArrayView<const uint8> BlockSignature)
{
	if (PublicKey == InvalidRSAKeyHandle)
	{
		return FIoStatus(EIoErrorCode::SignatureError, TEXT("Invalid signing key"));
	}

	TArray<uint8> DecryptedTocHash, DecryptedBlocksHash;

	int32 BytesDecrypted = GetEngineCrypto()->DecryptPublic(TocSignature, DecryptedTocHash, PublicKey);
	if (BytesDecrypted != UE_ARRAY_COUNT(FSHAHash::Hash))
	{
		return FIoStatus(EIoErrorCode::SignatureError, TEXT("Failed to decrypt TOC signature"));
	}

	BytesDecrypted = GetEngineCrypto()->DecryptPublic(BlockSignature, DecryptedBlocksHash, PublicKey);
	if (BytesDecrypted != UE_ARRAY_COUNT(FSHAHash::Hash))
	{
		return FIoStatus(EIoErrorCode::SignatureError, TEXT("Failed to decrypt block signature"));
	}

	FSHAHash TocHash, BlocksHash;
	FSHA1::HashBuffer(reinterpret_cast<const uint8*>(&TocHeader), sizeof(FIoStoreTocHeader), TocHash.Hash);
	FSHA1::HashBuffer(BlockSignatureHashes.GetData(), BlockSignatureHashes.Num() * sizeof(FSHAHash), BlocksHash.Hash);

	if (FMemory::Memcmp(DecryptedTocHash.GetData(), TocHash.Hash, DecryptedTocHash.Num()) != 0)
	{
		return FIoStatus(EIoErrorCode::SignatureError, TEXT("Invalid TOC signature"));
	}

	if (FMemory::Memcmp(DecryptedBlocksHash.GetData(), BlocksHash.Hash, DecryptedBlocksHash.Num()) != 0)
	{
		return FIoStatus(EIoErrorCode::SignatureError, TEXT("Invalid block signature"));
	}

	return FIoStatus::Ok;
}

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
	FGraphEventRef CompressionEncryptionTask;
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
		Block->CompressionEncryptionTask = FGraphEventRef(); 
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

	FIoStatus Initialize(FIoStoreWriterContextImpl& InContext, const TCHAR* InFileName, const FIoContainerSettings& InContainerSettings)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(InitializeChunkWriter);
		WriterContext = &InContext;
		ContainerSettings = InContainerSettings;
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

					if (CurrentWriteQueueItem->CompressionEncryptionTask.IsValid())
					{
						FTaskGraphInterface::Get().WaitUntilTaskCompletes(CurrentWriteQueueItem->CompressionEncryptionTask);
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
					if (ContainerSettings.IsSigned())
					{
						FSHAHash& BlockHash = BlockSignatureHashes.AddDefaulted_GetRef();
						FSHA1::HashBuffer(SourceData, SourceDataSize, BlockHash.Hash);
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

		if (ContainerSettings.IsCompressed() && WriteOptions.bForceUncompressed)
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

	TArrayView<const FSHAHash> GetBlockSignatureHashes()
	{
		return BlockSignatureHashes;
	}

private:
	void CompressEncryptBlock(FIoStoreWriterBlock* Block)
	{
		if (ContainerSettings.IsCompressed())
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

			if (ContainerSettings.IsEncrypted())
			{
				// Fill the trailing buffer with bytes from the block. Note that this is now from a fixed location
				// rather than a random one so that we produce deterministic results (PakFileUtilities.cpp)
				const int32 EncryptedSize = Align(CompressedSize, FAES::AESBlockSize);
				for (int32 FillIndex = CompressedSize; FillIndex < EncryptedSize; ++FillIndex)
				{
					Block->CompressedData.Data()[FillIndex] = Block->CompressedData.Data()[(FillIndex - CompressedSize) % CompressedSize];
				}
				CompressedSize = EncryptedSize;
			}

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

		if (ContainerSettings.IsEncrypted())
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(EncryptBlock);
			if (Block->CompressionMethod == NAME_None)
			{
				FAES::EncryptData(Block->UncompressedData.Data(), static_cast<uint32>(Block->UncompressedData.DataSize()), ContainerSettings.EncryptionKey);
			}
			else
			{
				FAES::EncryptData(Block->CompressedData.Data(), static_cast<uint32>(Block->CompressedSize), ContainerSettings.EncryptionKey);
			}
		}
	}

	void FlushCurrentBlock()
	{
		if (!CurrentBlock)
		{
			return;
		}
		const bool bIsCompressed = ContainerSettings.IsCompressed() && !CurrentBlock->bForceUncompressed;
		uint8* UncompressedData = CurrentBlock->UncompressedData.Data();
		const uint64 UncompressedDataSize = CurrentBlock->UncompressedData.DataSize();
		if (CurrentBlockOffset < UncompressedDataSize)
		{
			uint64 PadCount = UncompressedDataSize - CurrentBlockOffset;
			if (ContainerSettings.IsEncrypted() && !bIsCompressed)
			{
				// When the block is encrypted but NOT compressed we fill the trailing bytes
				// with data from the block. When the block IS compressed the trailing bytes
				// will get filled after compression.
				for (uint64 FillIndex = CurrentBlockOffset; FillIndex < UncompressedDataSize; ++FillIndex)
				{
					UncompressedData[FillIndex] = UncompressedData[FillIndex - CurrentBlockOffset];
				}
			}
			else
			{
				FMemory::Memzero(UncompressedData + CurrentBlockOffset, PadCount);
			}
			UncompressedOffset += PadCount;
		}
		if (bIsCompressed || ContainerSettings.IsEncrypted())
		{
			CurrentBlock->CompressionEncryptionTask = FFunctionGraphTask::CreateAndDispatchWhenReady([this, Block = CurrentBlock]()
			{
				CompressEncryptBlock(Block);
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
	FIoContainerSettings ContainerSettings;
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
	TArray<FSHAHash> BlockSignatureHashes;
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

	UE_NODISCARD FIoStatus Initialize(FIoStoreWriterContextImpl& InContext, const FIoContainerSettings& InContainerSettings)
	{
		ContainerSettings = InContainerSettings;

		FString TocFilePath = Environment.GetPath() + TEXT(".utoc");
		FString ContainerFilePath = Environment.GetPath() + TEXT(".ucas");

		Result.ContainerId = ContainerId;
		Result.ContainerName = FPaths::GetBaseFilename(Environment.GetPath());
		Result.ContainerFlags = InContainerSettings.ContainerFlags;
		Result.CompressionMethod = EnumHasAnyFlags(InContainerSettings.ContainerFlags, EIoContainerFlags::Compressed)
			? InContext.Settings().CompressionMethod
			: NAME_None;

		IPlatformFile& Ipf = IPlatformFile::GetPlatformPhysical();
		Ipf.CreateDirectoryTree(*FPaths::GetPath(ContainerFilePath));

		ChunkWriter = MakeUnique<FChunkWriter>();
		FIoStatus Status = ChunkWriter->Initialize(InContext, *ContainerFilePath, InContainerSettings);

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
		TocHeader.EncryptionKeyGuid = ContainerSettings.EncryptionKeyGuid;
		TocHeader.ContainerFlags = ContainerSettings.ContainerFlags;

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

		if (EnumHasAnyFlags(TocHeader.ContainerFlags, EIoContainerFlags::Signed))
		{
			TArray<uint8> TocSignature, BlockSignature;
			TArrayView<const FSHAHash> BlockSignatureHashes = ChunkWriter->GetBlockSignatureHashes();
			check(BlockSignatureHashes.Num() == CompressionInfo.BlockEntries.Num());

			FIoStatus SignatureStatus = CreateContainerSignature(
				ContainerSettings.SigningKey,
				TocHeader,
				BlockSignatureHashes,
				TocSignature,
				BlockSignature);

			if (!SignatureStatus .IsOk())
			{
				return SignatureStatus;
			}

			check(TocSignature.Num() == BlockSignature.Num());

			const int32 HashSize = TocSignature.Num();
			TocFileHandle->Write(reinterpret_cast<const uint8*>(&HashSize), sizeof(int32));
			TocFileHandle->Write(TocSignature.GetData(), TocSignature.Num());
			TocFileHandle->Write(BlockSignature.GetData(), BlockSignature.Num());
			TocFileHandle->Write(reinterpret_cast<const uint8*>(BlockSignatureHashes.GetData()), BlockSignatureHashes.Num() * sizeof(FSHAHash));
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
	FIoContainerSettings				ContainerSettings;
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

FIoStatus FIoStoreWriter::Initialize(const FIoStoreWriterContext& Context, const FIoContainerSettings& ContainerSettings)
{
	return Impl->Initialize(*Context.Impl, ContainerSettings);
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

	UE_NODISCARD FIoStatus Initialize(const FIoStoreEnvironment& InEnvironment, const FIoContainerSettings& InContainerSettings)
	{
		ContainerSettings = InContainerSettings;

		TStringBuilder<256> ContainerFilePath;
		ContainerFilePath.Append(InEnvironment.GetPath());

		TStringBuilder<256> TocFilePath;
		TocFilePath.Append(ContainerFilePath);

		ContainerFilePath.Append(TEXT(".ucas"));
		TocFilePath.Append(TEXT(".utoc"));

		IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();
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

		ContainerFlags = TocReader.GetContainerFlags();
		check(!EnumHasAnyFlags(ContainerSettings.ContainerFlags, EIoContainerFlags::Encrypted) || ContainerSettings.EncryptionKeyGuid == TocReader.GetEncryptionKeyGuid());

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
			if (EnumHasAnyFlags(ContainerSettings.ContainerFlags, EIoContainerFlags::Encrypted))
			{
				FAES::DecryptData(CompressedBuffer.GetData(), static_cast<uint32>(CompressionBlock.OffsetAndLength.GetLength()), ContainerSettings.EncryptionKey);
			}
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
	
	FIoContainerSettings ContainerSettings;
	TArray<FIoStoreTocEntry> TocEntries;
	TArray<FIoStoreTocPartialEntry> TocPartialEntries;
	TMap<FIoChunkId, int32> TocMap;
	TArray<FCompressionBlock> CompressionBlocks;
	uint64 CompressionBlockSize = 0;
	TUniquePtr<IFileHandle> ContainerFileHandle;
	mutable TArray<uint8> CompressedBuffer;
	mutable TArray<uint8> UncompressedBuffer;
	EIoContainerFlags ContainerFlags;
};

FIoStoreReader::FIoStoreReader()
	: Impl(new FIoStoreReaderImpl())
{
}

FIoStoreReader::~FIoStoreReader()
{
	delete Impl;
}

FIoStatus FIoStoreReader::Initialize(const FIoStoreEnvironment& InEnvironment, const FIoContainerSettings& InContainerSettings)
{
	return Impl->Initialize(InEnvironment, InContainerSettings);
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
	
	if (IsSigningEnabled())
	{
		if (!EnumHasAnyFlags(Header->ContainerFlags, EIoContainerFlags::Signed))
		{
			return FIoStatus(EIoErrorCode::SignatureError, TEXT("Missing signature"));
		}

		const uint8* SignatureBuffer = reinterpret_cast<const uint8*>(AnsiCompressionMethodNames + Header->CompressionMethodNameCount * Header->CompressionMethodNameLength);
		const int32* HashSize = reinterpret_cast<const int32*>(SignatureBuffer);
		TArrayView<const uint8> TocSignature = MakeArrayView<const uint8>(reinterpret_cast<const uint8*>(HashSize + 1), *HashSize);
		TArrayView<const uint8> BlockSignature = MakeArrayView<const uint8>(TocSignature.GetData() + *HashSize, *HashSize);
		BlockSignatureHashes = MakeArrayView<const FSHAHash>(reinterpret_cast<const FSHAHash*>(BlockSignature.GetData() + *HashSize), CompressedBlockEntryCount);

		return ValidateContainerSignature(GetPublicSigningKey(), *Header, BlockSignatureHashes, TocSignature, BlockSignature);
	}

	return FIoStatus::Ok;
}
