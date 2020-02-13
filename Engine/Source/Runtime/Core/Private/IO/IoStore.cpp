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

//////////////////////////////////////////////////////////////////////////

constexpr char FIoStoreTocHeader::TocMagicImg[];

//////////////////////////////////////////////////////////////////////////

FIoStoreEnvironment::FIoStoreEnvironment()
{
}

FIoStoreEnvironment::~FIoStoreEnvironment()
{
}

void FIoStoreEnvironment::InitializeFileEnvironment(FStringView InPath)
{
	Path = InPath;
}

//////////////////////////////////////////////////////////////////////////

class FChunkWriter
{
public:
	virtual ~FChunkWriter() {}

	FIoStatus Initialize(const FIoStoreWriterSettings& InWriterSettings, const TCHAR* Filename)
	{
		WriterSettings = InWriterSettings;

		IPlatformFile& Ipf = IPlatformFile::GetPlatformPhysical();
		FileHandle.Reset(Ipf.OpenWrite(Filename, /* append */ false, /* allowread */ true));

		return FileHandle ? InitializeWriter() : FIoStatus(EIoErrorCode::FileNotOpen, TEXT("Failed to open container file handle"));
	}

	const FIoStoreWriterSettings& Settings()
	{
		return WriterSettings;
	}

	TIoStatusOr<FIoStoreTocEntry> Write(FIoChunkId ChunkId, FIoBuffer Chunk)
	{
		return WriteChunk(ChunkId, Chunk);
	}

	FIoStatus Flush()
	{
		CompressionInfo.UncompressedContainerSize = FileHandle.IsValid() ? FileHandle->Tell() : 0;
		CompressionInfo.CompressedContainerSize = 0;

		return FlushWriter();
	}

	const FIoStoreCompressionInfo& GetCompressionInfo()
	{
		return CompressionInfo;
	}

protected:
	FChunkWriter() { }

	virtual FIoStatus InitializeWriter()
	{
		return FIoStatus::Ok;
	}

	virtual TIoStatusOr<FIoStoreTocEntry> WriteChunk(FIoChunkId ChunkId, FIoBuffer Chunk) = 0;

	virtual FIoStatus FlushWriter()
	{
		return FIoStatus::Ok;
	}

	FIoStoreWriterSettings WriterSettings;
	FIoStoreCompressionInfo CompressionInfo;
	TUniquePtr<IFileHandle> FileHandle;
};

class FDefaultChunkWriter
	: public FChunkWriter
{
private:
	virtual TIoStatusOr<FIoStoreTocEntry> WriteChunk(FIoChunkId ChunkId, FIoBuffer Chunk) override
	{
		FIoStoreTocEntry TocEntry;
		TocEntry.SetOffset(FileHandle->Tell());
		TocEntry.SetLength(Chunk.DataSize());
		TocEntry.ChunkId = ChunkId;

		const bool bSuccess = FileHandle->Write(Chunk.Data(), Chunk.DataSize());

		return bSuccess
			? TIoStatusOr<FIoStoreTocEntry>(TocEntry)
			: FIoStatus(EIoErrorCode::FileNotOpen, TEXT("Failed to write to container file"));
	}
};

class FCompressedChunkWriter
	: public FChunkWriter
{
	static constexpr int32 NumParallelCompressionBlocks = 32;	

public:
	virtual ~FCompressedChunkWriter()
	{
		check(WriterBuffers->UncompressedBufferWriter->Tell() == 0);
	}

private:
	virtual FIoStatus InitializeWriter() override
	{
		const int32 UncompressedBufferSize = int32(WriterSettings.CompressionBlockSize * NumParallelCompressionBlocks);

		for (FCompressionBuffers& Buffers : CompressionBuffers)
		{
			Buffers.UncompressedBuffer.SetNumZeroed(UncompressedBufferSize);
			Buffers.CompressedBlocks.SetNum(NumParallelCompressionBlocks);
			Buffers.UncompressedBufferWriter = MakeUnique<FBufferWriter>(Buffers.UncompressedBuffer.GetData(), UncompressedBufferSize);
			Buffers.UncompressedBlockCount = 0;
			Buffers.CompressedBlockCount = 0;
		}

		WriterBuffers = &CompressionBuffers[0];
		CompressorBuffers = &CompressionBuffers[1];

		return FIoStatus::Ok;
	}

	virtual TIoStatusOr<FIoStoreTocEntry> WriteChunk(FIoChunkId ChunkId, FIoBuffer Chunk) override
	{
		const int64 ChunkSize = int64(Chunk.DataSize());
		const uint8* ChunkData = Chunk.Data();

		FIoStoreTocEntry TocEntry;
		TocEntry.SetOffset(UncompressedFileOffset);
		TocEntry.SetLength(ChunkSize);
		TocEntry.ChunkId = ChunkId;

		UncompressedFileOffset += ChunkSize;
		
		int64 RemainingBytesInChunk = ChunkSize;

		while (RemainingBytesInChunk > 0)
		{
			if (WriterBuffers->UncompressedBufferWriter->AtEnd())
			{
				CompressAndSerializeBuffer();
			}

			FBufferWriter* UncompressedBufferWriter = WriterBuffers->UncompressedBufferWriter.Get();
			const int64 RemainingBytesInBuffer = UncompressedBufferWriter->TotalSize() - UncompressedBufferWriter->Tell();
			check(RemainingBytesInBuffer > 0);

			const int64 BytesToWrite = FMath::Min(RemainingBytesInBuffer, RemainingBytesInChunk);

			UncompressedBufferWriter->Serialize((void*)(ChunkData), BytesToWrite);

			ChunkData += BytesToWrite;
			RemainingBytesInChunk -= BytesToWrite;
		}

		if (WriterBuffers->UncompressedBufferWriter->AtEnd())
		{
			CompressAndSerializeBuffer();
		}

		return TIoStatusOr<FIoStoreTocEntry>(TocEntry);
	}

	virtual FIoStatus FlushWriter() override
	{
		const int64 BytesInBlock = WriterBuffers->UncompressedBufferWriter->Tell() % WriterSettings.CompressionBlockSize;
		if (BytesInBlock > 0)
		{
			const int64 RemainingBytesInBlock = WriterSettings.CompressionBlockSize - BytesInBlock;
			WriterBuffers->UncompressedBufferWriter->Seek(WriterBuffers->UncompressedBufferWriter->Tell() + RemainingBytesInBlock);

			CompressAndSerializeBuffer();
			CompressionResult.Wait();
			CompressAndSerializeBuffer();

			check(WriterBuffers->UncompressedBufferWriter->Tell() == 0);
		}

		CompressionInfo.UncompressedContainerSize = UncompressedFileOffset;
		CompressionInfo.CompressedContainerSize = FileHandle->Tell();

		return FIoStatus::Ok;
	}

	void CompressAndSerializeBuffer()
	{
		WriterBuffers->UncompressedBlockCount = int32(WriterBuffers->UncompressedBufferWriter->Tell() / WriterSettings.CompressionBlockSize);
		
		CompressionResult.Wait();

		Swap(WriterBuffers, CompressorBuffers);

		if (CompressorBuffers->UncompressedBlockCount > 0)
		{
			CompressionResult = Async(EAsyncExecution::Thread, [this]()
			{
				ParallelFor(CompressorBuffers->UncompressedBlockCount, [this](const int32 BlockIndex)
				{
					const uint8* UncompressedBlock = CompressorBuffers->UncompressedBuffer.GetData() + BlockIndex * WriterSettings.CompressionBlockSize;

					int32 CompressedSize = FCompression::CompressMemoryBound(WriterSettings.CompressionMethod, int32(WriterSettings.CompressionBlockSize));

					FCompressedBlock& CompressedBlock = CompressorBuffers->CompressedBlocks[BlockIndex];
					CompressedBlock.Buffer.SetNumZeroed(CompressedSize, false);

					const bool bCompressed = FCompression::CompressMemory(
						WriterSettings.CompressionMethod,
						CompressedBlock.Buffer.GetData(),
						CompressedSize,
						UncompressedBlock,
						int32(WriterSettings.CompressionBlockSize));

					check(bCompressed);
					check(CompressedSize > 0);

					CompressedBlock.Buffer.SetNum(CompressedSize, false);
				});

				CompressorBuffers->CompressedBlockCount = CompressorBuffers->UncompressedBlockCount;
				CompressorBuffers->UncompressedBlockCount = 0;
			});
		}

		check(WriterBuffers->UncompressedBlockCount == 0);

		for (int32 BlockIndex = 0; BlockIndex < WriterBuffers->CompressedBlockCount; BlockIndex++)
		{
			FCompressedBlock& CompressedBlock = WriterBuffers->CompressedBlocks[BlockIndex];

			const int64 CompressedFileOffset = FileHandle->Tell();

			const uint8* CompressedBuffer = CompressedBlock.Buffer.GetData();
			int32 CompressedSize = CompressedBlock.Buffer.Num();
			FName CompressionMethod = WriterSettings.CompressionMethod;

			if (CompressedBlock.Buffer.Num() > WriterSettings.CompressionBlockSize)
			{
				CompressedBuffer = WriterBuffers->UncompressedBuffer.GetData() + BlockIndex * WriterSettings.CompressionBlockSize;
				CompressedSize = int32(WriterSettings.CompressionBlockSize);
				CompressionMethod = NAME_None;
			}

			check(CompressedSize <= int32(WriterSettings.CompressionBlockSize));

			FileHandle->Write(CompressedBuffer, CompressedSize);

			FIoStoreCompressedBlockEntry CompressedBlockEntry;
			CompressedBlockEntry.OffsetAndLength.SetOffset(CompressedFileOffset);
			CompressedBlockEntry.OffsetAndLength.SetLength(CompressedSize);
			CompressedBlockEntry.CompressionMethodIndex = CompressionInfo.GetCompressionMethodIndex(CompressionMethod);

			CompressionInfo.BlockEntries.Emplace(CompressedBlockEntry);
		}
		
		WriterBuffers->UncompressedBufferWriter->Seek(0);
		WriterBuffers->CompressedBlockCount = 0;
		FMemory::Memzero(WriterBuffers->UncompressedBuffer.GetData(), WriterBuffers->UncompressedBuffer.Num());
	}

	struct FCompressedBlock
	{
		TArray<uint8> Buffer;
		FName CompressionMethod;
	};

	struct FCompressionBuffers
	{
		TArray<uint8> UncompressedBuffer;
		TArray<FCompressedBlock> CompressedBlocks;
		int32 UncompressedBlockCount = 0;
		int32 CompressedBlockCount = 0;
		TUniquePtr<FBufferWriter> UncompressedBufferWriter;
	};

	FCompressionBuffers CompressionBuffers[2];
	FCompressionBuffers* WriterBuffers = nullptr;
	FCompressionBuffers* CompressorBuffers = nullptr;
	TFuture<void> CompressionResult;
	int64 UncompressedFileOffset;
};

//////////////////////////////////////////////////////////////////////////

class FIoStoreWriterImpl
{
public:
	FIoStoreWriterImpl(FIoStoreEnvironment& InEnvironment)
	:	Environment(InEnvironment)
	{
	}

	UE_NODISCARD FIoStatus Initialize(const FIoStoreWriterSettings& InIoWriterSettings)
	{
		IPlatformFile& Ipf = IPlatformFile::GetPlatformPhysical();

		FString TocFilePath = Environment.GetPath() + TEXT(".utoc");
		FString ContainerFilePath = Environment.GetPath() + TEXT(".ucas");

		Result.ContainerName = FPaths::GetBaseFilename(Environment.GetPath());
		Result.CompressionMethod = InIoWriterSettings.CompressionMethod;

		Ipf.CreateDirectoryTree(*FPaths::GetPath(ContainerFilePath));

		if (InIoWriterSettings.CompressionMethod != NAME_None)
		{
			ChunkWriter = MakeUnique<FCompressedChunkWriter>();
		}
		else
		{
			ChunkWriter = MakeUnique<FDefaultChunkWriter>();
		}

		FIoStatus Status = ChunkWriter->Initialize(InIoWriterSettings, *ContainerFilePath);

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

		if (InIoWriterSettings.bEnableCsvOutput)
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

	UE_NODISCARD FIoStatus Append(FIoChunkId ChunkId, FIoBuffer Chunk, const TCHAR* Name)
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

		TIoStatusOr<FIoStoreTocEntry> TocEntryStatus = ChunkWriter->Write(ChunkId, Chunk);

		if (TocEntryStatus.IsOk())
		{
			FIoStoreTocEntry TocEntry = TocEntryStatus.ConsumeValueOrDie();
			Toc.Add(ChunkId, TocEntry);

			if (CsvArchive)
			{
				ANSICHAR Line[MAX_SPRINTF];
				FCStringAnsi::Sprintf(Line, "%s,%lld,%lld\n", TCHAR_TO_ANSI(Name), TocEntry.GetOffset(), TocEntry.GetLength());
				CsvArchive->Serialize(Line, FCStringAnsi::Strlen(Line));
			}

			return FIoStatus::Ok;
		}
		else
		{
			return FIoStatus(EIoErrorCode::WriteError, TEXT("Append failed"));
		}
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

		FIoStoreTocEntry TocEntry;

		TocEntry.SetOffset(Entry->GetOffset() + Offset);
		TocEntry.SetLength(Length);
		TocEntry.ChunkId = ChunkIdPartialRange;

		Toc.Add(ChunkIdPartialRange, TocEntry);

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
		TocHeader.CompressionBlockCount = CompressionInfo.BlockEntries.Num();
		TocHeader.CompressionBlockSize = uint32(ChunkWriter->Settings().CompressionBlockSize);
		TocHeader.CompressionNameCount = CompressionInfo.CompressionMethods.Num();

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

		// Compression blocks
		for (const FIoStoreCompressedBlockEntry& CompressedBlockEntry : CompressionInfo.BlockEntries)
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
		Result.TocEntryCount = TocHeader.TocEntryCount;
		Result.UncompressedContainerSize = CompressionInfo.UncompressedContainerSize;
		Result.CompressedContainerSize = CompressionInfo.CompressedContainerSize;

		return Result;
	}

private:
	FIoStoreEnvironment&				Environment;
	TMap<FIoChunkId, FIoStoreTocEntry>	Toc;
	TUniquePtr<FChunkWriter>			ChunkWriter;
	TUniquePtr<IFileHandle>				TocFileHandle;
	TUniquePtr<FArchive>				CsvArchive;
	FIoStoreWriterResult				Result;
	bool								IsMetadataDirty = true;
};

FIoStoreWriter::FIoStoreWriter(FIoStoreEnvironment& InEnvironment)
:	Impl(new FIoStoreWriterImpl(InEnvironment))
{
}

FIoStoreWriter::~FIoStoreWriter()
{
	TIoStatusOr<FIoStoreWriterResult> Status = Impl->Flush();
	check(Status.IsOk());
}

FIoStatus FIoStoreWriter::Initialize(const FIoStoreWriterSettings& Settings)
{
	return Impl->Initialize(Settings);
}

FIoStatus FIoStoreWriter::Append(FIoChunkId ChunkId, FIoBuffer Chunk, const TCHAR* Name)
{
	return Impl->Append(ChunkId, Chunk, Name);
}

FIoStatus FIoStoreWriter::MapPartialRange(FIoChunkId OriginalChunkId, uint64 Offset, uint64 Length, FIoChunkId ChunkIdPartialRange)
{
	return Impl->MapPartialRange(OriginalChunkId, Offset, Length, ChunkIdPartialRange);
}

TIoStatusOr<FIoStoreWriterResult> FIoStoreWriter::Flush()
{
	return Impl->Flush();
}
