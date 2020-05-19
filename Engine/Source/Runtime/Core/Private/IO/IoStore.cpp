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

struct FChunkBlock
{
	uint64 Offset = 0;
	uint64 Size = 0;
	uint64 UncompressedSize = 0;
};

struct FIoStoreWriteQueueEntry
{
	FIoStoreWriteQueueEntry* Next = nullptr;
	FIoChunkId ChunkId;
	FIoChunkHash ChunkHash;
	FIoBuffer ChunkBuffer;
	uint64 ChunkSize = 0;
	TArray<FChunkBlock> ChunkBlocks;
	FIoWriteOptions Options;
	FGraphEventRef CreateChunkBlocksTask = FGraphEventRef();
	FName CompressionMethod = NAME_None;
};

class FIoStoreWriteQueue
{
public:
	FIoStoreWriteQueue()
		: Event(FPlatformProcess::GetSynchEventFromPool(false))
	{ }
	
	~FIoStoreWriteQueue()
	{
		check(Head == nullptr && Tail == nullptr);
		FPlatformProcess::ReturnSynchEventToPool(Event);
	}

	void Enqueue(FIoStoreWriteQueueEntry* Entry)
	{
		check(!bIsDoneAdding);
		{
			FScopeLock _(&CriticalSection);

			if (!Tail)
			{
				Head = Tail = Entry;
			}
			else
			{
				Tail->Next = Entry;
				Tail = Entry;
			}
		}

		Event->Trigger();
	}

	FIoStoreWriteQueueEntry* DequeueOrWait()
	{
		for (;;)
		{
			{
				FScopeLock _(&CriticalSection);
				if (Head)
				{
					FIoStoreWriteQueueEntry* Entry = Head;
					Head = Tail = nullptr;
					return Entry;
				}
			}

			if (bIsDoneAdding)
			{
				break;
			}

			Event->Wait();
		}

		return nullptr;
	}

	void CompleteAdding()
	{
		bIsDoneAdding = true;
		Event->Trigger();
	}

	bool IsDoneAdding() const
	{
		return bIsDoneAdding;
	}

	bool IsEmpty() const
	{
		FScopeLock _(&CriticalSection);
		return Head == nullptr;
	}

private:
	mutable FCriticalSection CriticalSection;
	FEvent* Event = nullptr;
	FIoStoreWriteQueueEntry* Head = nullptr;
	FIoStoreWriteQueueEntry* Tail = nullptr;
	TAtomic<bool> bIsDoneAdding { false };
};

class FIoStoreWriterContextImpl
{
	static constexpr uint64 DefaultMemoryLimit = 5ull * (2ull << 30ull);
public:
	FIoStoreWriterContextImpl()
	{
	}

	~FIoStoreWriterContextImpl()
	{
		if (MemoryFreedEvent)
		{
			FPlatformProcess::ReturnSynchEventToPool(MemoryFreedEvent);
		}
	}

	UE_NODISCARD FIoStatus Initialize(const FIoStoreWriterSettings& InWriterSettings)
	{
		WriterSettings = InWriterSettings;
		MemoryFreedEvent = FPlatformProcess::GetSynchEventFromPool(false);

		if (InWriterSettings.WriterMemoryLimit > 0)
		{
			NumBytesAvailable = InWriterSettings.WriterMemoryLimit;
		}

		return FIoStatus::Ok;
	}

	const FIoStoreWriterSettings& GetSettings() const
	{
		return WriterSettings;
	}

	FIoStoreWriteQueueEntry* AllocQueueEntry(
		const FIoChunkId& ChunkId,
		const FIoChunkHash& ChunkHash,
		FIoBuffer ChunkBuffer,
		const FIoWriteOptions& Options)
	{
		const uint64 ChunkSize = ChunkBuffer.DataSize();

		for (;;)
		{
			{
				FScopeLock _(&CriticalSection);
				if (NumBytesAvailable > ChunkSize)
				{
					NumBytesAvailable -= ChunkSize;

					const uint64 AlignedChunkSize = Align(ChunkSize, FAES::AESBlockSize);
					FIoBuffer AlignedBuffer(AlignedChunkSize);
					FMemory::Memcpy(AlignedBuffer.Data(), ChunkBuffer.Data(), ChunkSize);

					for (uint64 FillIndex = ChunkSize; FillIndex < AlignedChunkSize; ++FillIndex)
					{
						AlignedBuffer.Data()[FillIndex] = AlignedBuffer.Data()[(FillIndex - ChunkSize) % ChunkSize];
					}

					FIoStoreWriteQueueEntry* Entry = new FIoStoreWriteQueueEntry();
					Entry->ChunkId = ChunkId;
					Entry->ChunkHash = ChunkHash;
					Entry->ChunkBuffer = AlignedBuffer;
					Entry->ChunkSize = ChunkSize;
					Entry->Options = Options;

					return Entry;
				}
			}
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(WaitForMemory);
				MemoryFreedEvent->Wait();
			}
		}
	}

	void FreeQueueEntry(FIoStoreWriteQueueEntry* QueueEntry)
	{
		{
			FScopeLock _(&CriticalSection);
			NumBytesAvailable += QueueEntry->ChunkSize;
		}

		delete QueueEntry;
		MemoryFreedEvent->Trigger();
	}

private:
	FIoStoreWriterSettings WriterSettings;
	FCriticalSection CriticalSection;
	FEvent* MemoryFreedEvent = nullptr;
	uint64 NumBytesAvailable = DefaultMemoryLimit;
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
		WriterContext = &InContext;
		ContainerSettings = InContainerSettings;

		FString TocFilePath = Environment.GetPath() + TEXT(".utoc");
		FString ContainerFilePath = Environment.GetPath() + TEXT(".ucas");

		Result.ContainerId = ContainerId;
		Result.ContainerName = FPaths::GetBaseFilename(Environment.GetPath());
		Result.ContainerFlags = InContainerSettings.ContainerFlags;
		Result.CompressionMethod = EnumHasAnyFlags(InContainerSettings.ContainerFlags, EIoContainerFlags::Compressed)
			? InContext.GetSettings().CompressionMethod
			: NAME_None;

		IPlatformFile& Ipf = IPlatformFile::GetPlatformPhysical();
		Ipf.CreateDirectoryTree(*FPaths::GetPath(ContainerFilePath));

		ContainerFileHandle.Reset(Ipf.OpenWrite(*ContainerFilePath, /* append */ false, /* allowread */ true));

		if (!ContainerFileHandle)
		{
			return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to open IoStore container file '") << *ContainerFilePath << TEXT("'");
		}

		TocFileHandle.Reset(Ipf.OpenWrite(*TocFilePath, /* append */ false, /* allowread */ true));

		if (!TocFileHandle)
		{
			return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to open IoStore TOC file '") << *TocFilePath << TEXT("'");
		}

		FIoStatus Status = FIoStatus::Ok;
		if (InContext.GetSettings().bEnableCsvOutput)
		{
			Status = EnableCsvOutput();
		}

		WriterThread = Async(EAsyncExecution::Thread, [this]() { ProcessChunksThread(); });

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
		if (!ChunkId.IsValid())
		{
			return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("ChunkId is not valid!"));
		}

		IsMetadataDirty = true;

		FIoStoreWriteQueueEntry* Entry = WriterContext->AllocQueueEntry(ChunkId, ChunkHash, Chunk, WriteOptions);

		Entry->CreateChunkBlocksTask = FFunctionGraphTask::CreateAndDispatchWhenReady([this, Entry]()
		{ 
			CreateChunkBlocks(Entry, ContainerSettings, WriterContext->GetSettings());
		}, TStatId(), nullptr, ENamedThreads::AnyHiPriThreadHiPriTask);

		WriteQueue.Enqueue(Entry);

		return FIoStatus::Ok;
	}

	UE_NODISCARD FIoStatus AppendPadding(uint64 Count)
	{
		return FIoStatus::Ok;
	}

	UE_NODISCARD FIoStatus MapPartialRange(FIoChunkId OriginalChunkId, uint64 Offset, uint64 Length, FIoChunkId ChunkIdPartialRange)
	{
		if (!ChunkIdPartialRange.IsValid())
		{
			return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("ChunkIdPartialRange is not valid!"));
		}

		FIoStoreTocPartialEntry TocPartialEntry;

		TocPartialEntry.SetOffset(Offset);
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

		WriteQueue.CompleteAdding();
		WriterThread.Wait();

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
		TocHeader.CompressionBlockSize = uint32(WriterContext->GetSettings().CompressionBlockSize);
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
			FIoStoreTocPartialEntry& TocPartialEntry = _.Value;
			FIoStoreTocEntry* OriginalTocEntry = Toc.Find(TocPartialEntry.OriginalChunkId);

			if (!OriginalTocEntry)
			{
				return FIoStatus(EIoErrorCode::WriteError, TEXT("Failed find original chunk for partial TOC entry"));
			}

			if (TocPartialEntry.GetOffset() + TocPartialEntry.GetLength() > OriginalTocEntry->GetLength())
			{
				return FIoStatus(EIoErrorCode::WriteError, TEXT("Partial TOC entry larger than original chunk size"));
			}

			TocPartialEntry.SetOffset(OriginalTocEntry->GetOffset() + TocPartialEntry.GetOffset());

			if (!TocFileHandle->Write(reinterpret_cast<const uint8*>(&TocPartialEntry), sizeof(FIoStoreTocPartialEntry)))
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
		Result.PaddingSize = TotalPaddedBytes;
		Result.UncompressedContainerSize = CompressionInfo.UncompressedContainerSize;
		Result.CompressedContainerSize = CompressionInfo.CompressedContainerSize;

		return Result;
	}

private:
	void ProcessChunksThread()
	{
		const FIoStoreWriterSettings& Settings = WriterContext->GetSettings();
		TArray<uint8> PaddingBuffer;
		uint64 UncompressedFileOffset = 0;

		auto CrossesBlockBoundry = [](const uint64 Offset, const uint64 Size, const uint64 BlockSize) -> bool
		{
			return BlockSize > 0 ? Align(Offset, BlockSize) != Align(Offset + Size - 1, BlockSize) : false;
		};

		auto WritePadding = [&PaddingBuffer](IFileHandle& FileHandle, const uint64 BlockSize) -> uint64
		{
			const uint64 Padding = GetPadding(FileHandle.Tell(), BlockSize);
			if (Padding > 0)
			{
				PaddingBuffer.SetNumZeroed(int32(Padding), false);
				FileHandle.Write(PaddingBuffer.GetData(), Padding);
			}
			return Padding;
		};
		
		for (;;)
		{
			FIoStoreWriteQueueEntry* Entry = WriteQueue.DequeueOrWait();

			if (!Entry && WriteQueue.IsDoneAdding())
			{
				break;
			}

			while (Entry)
			{
				if (Entry->CreateChunkBlocksTask.IsValid())
				{
					FTaskGraphInterface::Get().WaitUntilTaskCompletes(Entry->CreateChunkBlocksTask);
				}

				if (CrossesBlockBoundry(ContainerFileHandle->Tell(), Entry->ChunkBuffer.DataSize() + Entry->Options.Alignment, Settings.CompressionBlockAlignment))
				{
					TotalPaddedBytes += WritePadding(*ContainerFileHandle, Settings.CompressionBlockAlignment);
				}

				if (Entry->Options.Alignment > 0)
				{
					TotalPaddedBytes += WritePadding(*ContainerFileHandle, Entry->Options.Alignment);
				}

				const uint64 FileOffset = ContainerFileHandle->Tell();

				FIoStoreTocEntry TocEntry;
				TocEntry.ChunkId = Entry->ChunkId;
				TocEntry.ChunkHash = Entry->ChunkHash;
				TocEntry.SetOffset(UncompressedFileOffset);
				TocEntry.SetLength(Entry->ChunkSize);

				Toc.Add(TocEntry.ChunkId, TocEntry);

				for (const FChunkBlock& ChunkBlock : Entry->ChunkBlocks)
				{
					check(ChunkBlock.Offset + ChunkBlock.Size <= Entry->ChunkBuffer.DataSize());

					FIoStoreTocCompressedBlockEntry& BlockEntry = CompressionInfo.BlockEntries.AddDefaulted_GetRef();
					BlockEntry.SetOffset(FileOffset + ChunkBlock.Offset);
					BlockEntry.SetSize(ChunkBlock.Size);
					BlockEntry.SetUncompressedSize(ChunkBlock.UncompressedSize);
					BlockEntry.SetCompressionMethodIndex(CompressionInfo.GetCompressionMethodIndex(Entry->CompressionMethod));

					if (ContainerSettings.IsSigned())
					{
						FSHAHash& BlockHash = BlockSignatureHashes.AddDefaulted_GetRef();
						FSHA1::HashBuffer(Entry->ChunkBuffer.Data() + ChunkBlock.Offset, ChunkBlock.Size, BlockHash.Hash);
					}
				}

				ContainerFileHandle->Write(Entry->ChunkBuffer.Data(), Entry->ChunkBuffer.DataSize());
				UncompressedFileOffset += Align(Entry->ChunkSize, Settings.CompressionBlockSize);

				FIoStoreWriteQueueEntry* Free = Entry;
				Entry = Entry->Next;
				WriterContext->FreeQueueEntry(Free);
			}
		}

		CompressionInfo.UncompressedContainerSize = UncompressedFileOffset + TotalPaddedBytes;
		CompressionInfo.CompressedContainerSize = ContainerFileHandle->Tell();

		check(WriteQueue.IsEmpty());
	}

	static void CreateChunkBlocks(
		FIoStoreWriteQueueEntry* Entry,
		const FIoContainerSettings& ContainerSettings,
		const FIoStoreWriterSettings& WriterSettings)
	{
		check(WriterSettings.CompressionBlockSize > 0);

		const uint64 UncompressedSize = Entry->ChunkBuffer.DataSize();
		const uint64 NumChunkBlocks = Align(UncompressedSize, WriterSettings.CompressionBlockSize) / WriterSettings.CompressionBlockSize;
		Entry->ChunkBlocks.Reserve(int32(NumChunkBlocks));

		auto CreateUncompressedBlocks = [](uint64 UncompressedChunkSize, const uint64 BlockSize, TArray<FChunkBlock>& OutChunkBlocks) -> void
		{
			uint64 UncompressedBytes = 0;
			while (UncompressedChunkSize)
			{
				const uint64 UncompressedBlockSize = FMath::Min<uint64>(UncompressedChunkSize, BlockSize);
				OutChunkBlocks.Add(FChunkBlock { UncompressedBytes, UncompressedBlockSize, UncompressedBlockSize  });
				UncompressedChunkSize -= UncompressedBlockSize;
				UncompressedBytes += UncompressedBlockSize;
			}
		};

		if (ContainerSettings.IsCompressed() && !Entry->Options.bForceUncompressed)
		{
			check(!WriterSettings.CompressionMethod.IsNone());

			const uint8* UncompressedBlock = Entry->ChunkBuffer.Data();
			TArray<TUniquePtr<uint8[]>> CompressedBlocks;
			CompressedBlocks.Reserve(int32(NumChunkBlocks));

			uint64 BytesToProcess	= UncompressedSize;
			uint64 CompressedBytes	= 0;

			while (BytesToProcess > 0)
			{
				const int32 UncompressedBlockSize = static_cast<int32>(FMath::Min(BytesToProcess, WriterSettings.CompressionBlockSize));
				int32 CompressedBlockSize = FCompression::CompressMemoryBound(WriterSettings.CompressionMethod, UncompressedBlockSize);
				TUniquePtr<uint8[]>& CompressedBlock = CompressedBlocks.AddDefaulted_GetRef();
				CompressedBlock = MakeUnique<uint8[]>(CompressedBlockSize);

				const bool bCompressed = FCompression::CompressMemory(
					WriterSettings.CompressionMethod,
					CompressedBlock.Get(),
					CompressedBlockSize,
					UncompressedBlock,
					UncompressedBlockSize);

				check(bCompressed);
				check(CompressedBlockSize > 0);

				if (!IsAligned(CompressedBlockSize, FAES::AESBlockSize))
				{
					const int32 AlignedCompressedBlockSize = Align(CompressedBlockSize, FAES::AESBlockSize);
					TUniquePtr<uint8[]> AlignedBlock = MakeUnique<uint8[]>(AlignedCompressedBlockSize);

					FMemory::Memcpy(AlignedBlock.Get(), CompressedBlock.Get(), CompressedBlockSize);

					for (uint64 FillIndex = CompressedBlockSize; FillIndex < AlignedCompressedBlockSize ; ++FillIndex)
					{
						AlignedBlock.Get()[FillIndex] = AlignedBlock.Get()[(FillIndex - CompressedBlockSize) % CompressedBlockSize];
					}

					CompressedBlock.Reset(AlignedBlock.Release());
					CompressedBlockSize = AlignedCompressedBlockSize;
				}

				Entry->ChunkBlocks.Add(FChunkBlock { CompressedBytes, uint64(CompressedBlockSize), uint64(UncompressedBlockSize) });

				BytesToProcess		-= UncompressedBlockSize;
				CompressedBytes		+= CompressedBlockSize;
				UncompressedBlock	+= UncompressedBlockSize;
			}

			const uint64 CompressedSize = Align(CompressedBytes, FAES::AESBlockSize);
			float PercentLess = ((float)CompressedSize / (UncompressedSize / 100.f));
			const bool bNotEnoughCompression = PercentLess > 90.f;

			if (bNotEnoughCompression)
			{
				Entry->ChunkBlocks.Empty();
				CreateUncompressedBlocks(UncompressedSize, WriterSettings.CompressionBlockSize,  Entry->ChunkBlocks);
				Entry->CompressionMethod = NAME_None;
			}
			else
			{
				Entry->ChunkBuffer = FIoBuffer(CompressedSize);
				Entry->CompressionMethod = WriterSettings.CompressionMethod;

				uint8* CompressedChunkBuffer = Entry->ChunkBuffer.Data();
				FMemory::Memzero(CompressedChunkBuffer, CompressedSize); 

				for (int32 BlockIndex = 0; BlockIndex < CompressedBlocks.Num(); ++BlockIndex)
				{
					TUniquePtr<uint8[]>& CompressedBlock = CompressedBlocks[BlockIndex];
					const FChunkBlock& ChunkBlock = Entry->ChunkBlocks[BlockIndex];
					FMemory::Memcpy(CompressedChunkBuffer, CompressedBlock.Get(), ChunkBlock.Size);
					CompressedChunkBuffer += ChunkBlock.Size;
				}
			}
		}
		else
		{
			CreateUncompressedBlocks(UncompressedSize, WriterSettings.CompressionBlockSize,  Entry->ChunkBlocks);
			Entry->CompressionMethod = NAME_None;
		}

		if (ContainerSettings.IsEncrypted())
		{
			for (const FChunkBlock& ChunkBlock : Entry->ChunkBlocks)
			{
				check(IsAligned(ChunkBlock.Size, FAES::AESBlockSize));
				FAES::EncryptData(Entry->ChunkBuffer.Data() + ChunkBlock.Offset, static_cast<uint32>(ChunkBlock.Size), ContainerSettings.EncryptionKey);
			}
		}
	}

	friend class FIoStoreWriter;
	FIoStoreEnvironment&				Environment;
	FIoStoreWriterContextImpl*			WriterContext = nullptr;
	TMap<FIoChunkId, FIoStoreTocEntry>	Toc;
	TMap<FIoChunkId, FIoStoreTocPartialEntry> PartialToc;
	TUniquePtr<IFileHandle>				TocFileHandle;
	TUniquePtr<IFileHandle>				ContainerFileHandle;
	TUniquePtr<FArchive>				CsvArchive;
	FIoStoreWriterResult				Result;
	FIoContainerId						ContainerId;
	FIoContainerSettings				ContainerSettings;
	TFuture<void>						WriterThread;
	FIoStoreWriteQueue					WriteQueue;
	FIoStoreCompressionInfo				CompressionInfo;
	TArray<FSHAHash>					BlockSignatureHashes;
	uint64								TotalPaddedBytes = 0;
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
		const FName* CompressionNames = TocReader.GetCompressionMethodNames(CompressionMethodNameCount);
		CompressionMethodNames = MakeArrayView<const FName>(CompressionNames, CompressionMethodNameCount);

		CompressionBlockSize = TocReader.GetCompressionBlockSize();
		CompressedBuffer.SetNumUninitialized(int32(CompressionBlockSize));
		UncompressedBuffer.SetNumUninitialized(int32(CompressionBlockSize));

		uint32 CompressedBlockEntryCount;
		const FIoStoreTocCompressedBlockEntry* CompressedBlockEntry = TocReader.GetCompressedBlockEntries(CompressedBlockEntryCount);
		CompressionBlocks.Reserve(CompressedBlockEntryCount);

		while (CompressedBlockEntryCount--)
		{
			CompressionBlocks.Emplace(*CompressedBlockEntry);
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
			const FIoStoreTocCompressedBlockEntry& CompressionBlock = CompressionBlocks[BlockIndex];
			if (CompressedBuffer.Num() < CompressionBlock.GetSize())
			{
				CompressedBuffer.SetNumUninitialized(int32(CompressionBlock.GetSize()));
			}
			ContainerFileHandle->Seek(CompressionBlock.GetOffset());
			ContainerFileHandle->Read(CompressedBuffer.GetData(), CompressionBlock.GetSize());
			if (EnumHasAnyFlags(ContainerSettings.ContainerFlags, EIoContainerFlags::Encrypted))
			{
				FAES::DecryptData(CompressedBuffer.GetData(), static_cast<uint32>(CompressionBlock.GetSize()), ContainerSettings.EncryptionKey);
			}
			if (CompressionBlock.GetCompressionMethodIndex() == 0)
			{
				Src = CompressedBuffer.GetData();
			}
			else
			{
				FName CompressionMethod = CompressionMethodNames[CompressionBlock.GetCompressionMethodIndex()];
				bool bUncompressed = FCompression::UncompressMemory(CompressionMethod, UncompressedBuffer.GetData(), int32(CompressionBlockSize), CompressedBuffer.GetData(), int32(CompressionBlock.GetSize()));
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
	FIoContainerSettings ContainerSettings;
	TArray<FIoStoreTocEntry> TocEntries;
	TArray<FIoStoreTocPartialEntry> TocPartialEntries;
	TMap<FIoChunkId, int32> TocMap;
	TArray<FIoStoreTocCompressedBlockEntry> CompressionBlocks;
	TArray<FName> CompressionMethodNames;
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
