// Copyright Epic Games, Inc. All Rights Reserved.

#if (IS_PROGRAM || WITH_EDITOR)

#include "IO/IoStoreOnDemand.h"

#include "Containers/ChunkedArray.h"
#include "HAL/FileManager.h"
#include "IO/IoChunkEncoding.h"
#include "IO/IoDispatcher.h"
#include "IO/IoStore.h"
#include "Memory/MemoryView.h"
#include "Misc/Paths.h"
#include "Tasks/Task.h"
#include "Tasks/Pipe.h"
#include "Templates/SharedPointer.h"
#include <atomic>

namespace UE::IO::Private
{

////////////////////////////////////////////////////////////////////////////////
struct FPendingWrite
{
	FIoChunkId ChunkId;
	TUniquePtr<IIoStoreWriteRequest> WriteRequest;
	FIoWriteOptions WriteOptions;
	FIoBuffer ChunkBuffer;
	FIoBuffer ChunkHeader;
	FIoHash ChunkRawHash;
	FIoHash ChunkHash;
	TArray<FIoHash> BlockHashes;
	FString ErrorText;
	uint64 RawSize{0};
	uint64 EncodedSize{0};
};

using FContainerEntries = TChunkedArray<FPendingWrite>;
class FOnDemandIoStoreWriter;

////////////////////////////////////////////////////////////////////////////////
struct FOnDemandContainer
	: public IIoStoreWriter
{
	FOnDemandContainer(FOnDemandIoStoreWriter& Writer, const FString& ContainerName, const FIoContainerSettings& ContainerSettings);

	virtual void SetReferenceChunkDatabase(TSharedPtr<IIoStoreWriterReferenceChunkDatabase> ReferenceChunkDatabase) override;
	virtual void SetHashDatabase(TSharedPtr<IIoStoreWriterHashDatabase> HashDatabase, bool bVerifyHashDatabase) override;
	virtual void EnableDiskLayoutOrdering(const TArray<TUniquePtr<FIoStoreReader>>& PatchSourceReaders = TArray<TUniquePtr<FIoStoreReader>>()) override;
	virtual void EnumerateChunks(TFunction<bool(FIoStoreTocChunkInfo&&)>&& Callback) const override;
	virtual void Append(const FIoChunkId& ChunkId, FIoBuffer Chunk, const FIoWriteOptions& WriteOptions, uint64 OrderHint = MAX_uint64) override;
	virtual void Append(const FIoChunkId& ChunkId, IIoStoreWriteRequest* Request, const FIoWriteOptions& WriteOptions) override;
	virtual TIoStatusOr<FIoStoreWriterResult> GetResult() override;

	FOnDemandIoStoreWriter& Writer;
	FString Name;
	FIoContainerSettings Settings;
	FContainerEntries Entries;
	TIoStatusOr<FIoStoreWriterResult> WriteResult;
};

////////////////////////////////////////////////////////////////////////////////
class FOnDemandIoStoreWriter
	: public IOnDemandIoStoreWriter
{
public:
	FOnDemandIoStoreWriter(const FIoStoreWriterSettings& WriterSettings, const FString& OutputDirectory, uint32 MaxConcurrentWrites);

	virtual TSharedPtr<IIoStoreWriter> CreateContainer(const FString& ContainerName, const FIoContainerSettings& ContainerSettings) override;
	virtual void Flush() override;

	void Append(
		const FString& ContainerName,
		const FIoContainerSettings& Settings,
		const FIoChunkId& ChunkId,
		IIoStoreWriteRequest* Request,
		const FIoWriteOptions& WriteOptions);

private:
	FIoStoreWriterSettings WriterSettings;
	FString OutputDirectory;
	FString ChunksDirectory;
	TMap<FString, TSharedPtr<FOnDemandContainer>> Containers;
	FCriticalSection ContainersCS;
	FEventRef WriteCompletedEvent;
	FEventRef WriteQueueEmptyEvent;
	UE::Tasks::FPipe TaskPipe;
	std::atomic<uint32> PendingCount{0};
	std::atomic<uint32> TotalCount{0};
	uint32 MaxConcurrentWrites;
};

FOnDemandIoStoreWriter::FOnDemandIoStoreWriter(const FIoStoreWriterSettings& Settings, const FString& InOutputDirectory, uint32 InMaxConcurrentWrites)
	: WriterSettings(Settings)
	, OutputDirectory(InOutputDirectory)
	, WriteQueueEmptyEvent(EEventMode::ManualReset)
	, TaskPipe(UE_SOURCE_LOCATION)
	, MaxConcurrentWrites(InMaxConcurrentWrites)
{
	ChunksDirectory = FString::Printf(TEXT("%s/IoChunksV%u"), *OutputDirectory, EOnDemandChunkVersion::Latest);
}

TSharedPtr<IIoStoreWriter> FOnDemandIoStoreWriter::CreateContainer(const FString& ContainerName, const FIoContainerSettings& ContainerSettings)
{
	TSharedPtr<FOnDemandContainer> Container = MakeShareable(new FOnDemandContainer(*this, ContainerName, ContainerSettings));
	Containers.Add(ContainerName, Container);

	return Container;
}

void FOnDemandIoStoreWriter::Flush() 
{
	WriteQueueEmptyEvent->Wait();

	FScopeLock _(&ContainersCS);
	TaskPipe.WaitUntilEmpty();

	FOnDemandToc TocResource;
	TocResource.Header.Magic = FOnDemandTocHeader::ExpectedMagic;
	TocResource.Header.Version = static_cast<uint32>(EOnDemandTocVersion::Latest);
	TocResource.Header.BlockSize = WriterSettings.CompressionBlockSize;
	TocResource.Header.CompressionFormat = WriterSettings.CompressionMethod.ToString();
	TocResource.Header.ChunksDirectory = FString::Printf(TEXT("IoChunksV%u"), EOnDemandChunkVersion ::Latest);

	for (auto& KV : Containers)
	{
		TSharedPtr<FOnDemandContainer>& Container = KV.Value;
		FContainerEntries& WriteEntries = Container->Entries;
		FOnDemandTocContainerEntry& ContainerEntry = TocResource.Containers.AddDefaulted_GetRef();
		FIoStoreWriterResult WriteResult; 
		
		ContainerEntry.ContainerName = Container->Name;
		WriteResult.ContainerId = FIoContainerId::FromName(FName(Container->Name));
		WriteResult.ContainerName = Container->Name;

		if (Container->Settings.IsCompressed())
		{
			WriteResult.CompressionMethod = WriterSettings.CompressionMethod;
			WriteResult.ContainerFlags = EIoContainerFlags::OnDemand | EIoContainerFlags::Compressed;
		}

		if (Container->Settings.IsEncrypted())
		{
			check(Container->Settings.EncryptionKey.IsValid());
			ContainerEntry.EncryptionKeyGuid = Container->Settings.EncryptionKeyGuid.ToString();
			WriteResult.ContainerFlags |= EIoContainerFlags::Encrypted;
		}

		FString Error;
		for (FPendingWrite& WriteEntry : WriteEntries)
		{
			if (!WriteEntry.ErrorText.IsEmpty())
			{
				UE_LOG(LogIoStore, Error, TEXT("%s"), *WriteEntry.ErrorText);
				Error = MoveTemp(WriteEntry.ErrorText);
				break;
			}

			FOnDemandTocEntry& TocEntry = ContainerEntry.Entries.AddDefaulted_GetRef();
			TocEntry.RawHash = WriteEntry.ChunkRawHash;
			TocEntry.Hash = WriteEntry.ChunkHash;
			TocEntry.ChunkId = WriteEntry.ChunkId;
			TocEntry.RawSize = WriteEntry.RawSize;
			TocEntry.EncodedSize = WriteEntry.EncodedSize;
			TocEntry.BlockOffset = ContainerEntry.BlockSizes.Num();

			const FIoChunkEncoding::FHeader* Header = FIoChunkEncoding::FHeader::Decode(WriteEntry.ChunkHeader.GetView());
			check(Header);
			TConstArrayView<uint32> Blocks = Header->GetBlocks();
			TArray<FIoHash> BlockHashes = MoveTemp(WriteEntry.BlockHashes);
			check(Blocks.Num() == BlockHashes.Num());

			TocEntry.BlockCount = Blocks.Num();
			ContainerEntry.BlockSizes.Append(Blocks);
			ContainerEntry.BlockHashes.Append(BlockHashes);

			WriteResult.UncompressedContainerSize += WriteEntry.RawSize;
			WriteResult.CompressedContainerSize += WriteEntry.EncodedSize;
			WriteResult.TocEntryCount++;
			WriteResult.TocSize += sizeof(FOnDemandTocEntry) + (sizeof(uint32) + Blocks.Num());
		}

		if (Error.Len())
		{
			Container->WriteResult = FIoStatus(EIoErrorCode::WriteError, Error);
		}
		else
		{
			Container->WriteResult = WriteResult;
		}
	}

	TIoStatusOr<FString> Status = FOnDemandToc::Save(*OutputDirectory, TocResource);
	if (Status.IsOk())
	{
		UE_LOG(LogIoStore, Display, TEXT("Saved ondemand TOC '%s'"), *Status.ConsumeValueOrDie());
	}
	else
	{
		UE_LOG(LogIoStore, Error, TEXT("Failed writing ondemand TOC, reason '%s'"), *Status.Status().ToString());
	}
}

void FOnDemandIoStoreWriter::Append(
	const FString& ContainerName,
	const FIoContainerSettings& ContainerSettings,
	const FIoChunkId& ChunkId,
	IIoStoreWriteRequest* Request,
	const FIoWriteOptions& WriteOptions)
{
	WriteQueueEmptyEvent->Reset();

	for(;;)
	{
		{
			FScopeLock _(&ContainersCS);

			if (PendingCount.load(std::memory_order_relaxed) < MaxConcurrentWrites)
			{
				PendingCount.fetch_add(1, std::memory_order_relaxed);
				
				TSharedPtr<FOnDemandContainer>& Container = Containers.FindChecked(ContainerName);
				FPendingWrite* PendingWrite = new(Container->Entries) FPendingWrite();
				PendingWrite->ChunkId = ChunkId;
				PendingWrite->WriteRequest.Reset(Request);
				PendingWrite->WriteOptions = WriteOptions;

				FGraphEventRef Event = FGraphEvent::CreateGraphEvent();
				PendingWrite->WriteRequest->PrepareSourceBufferAsync(Event);

				UE::Tasks::FTask ReadChunkTask = TaskPipe.Launch(TEXT("ReadChunk"),
					[PendingWrite, Event]() mutable
					{
						Event->Wait();
					});

				UE::Tasks::FTask EncodeChunkTask = TaskPipe.Launch(TEXT("EncodeChunk"),
					[this, PendingWrite, &ContainerSettings]() mutable
					{
						if (const FIoBuffer* SourceBuffer = PendingWrite->WriteRequest->GetSourceBuffer())
						{
							PendingWrite->ChunkBuffer = *SourceBuffer;
							PendingWrite->ChunkBuffer.EnsureOwned();
							PendingWrite->ChunkRawHash = FIoHash::HashBuffer(PendingWrite->ChunkBuffer.GetView());
							PendingWrite->RawSize = PendingWrite->ChunkBuffer.GetSize();

							PendingWrite->WriteRequest->FreeSourceBuffer();
							PendingWrite->WriteRequest.Reset();

							const FAES::FAESKey& Key = ContainerSettings.EncryptionKey;
							FMemoryView KeyView = Key.IsValid() ? MakeMemoryView(Key.Key, Key.KeySize) : FMemoryView();
							FIoChunkEncodingParams Params {WriterSettings.CompressionMethod, KeyView, uint32(WriterSettings.CompressionBlockSize)};

							const bool bEncoded = FIoChunkEncoding::Encode(Params, PendingWrite->ChunkBuffer.GetView(), PendingWrite->ChunkHeader, PendingWrite->ChunkBuffer);
							if (bEncoded)
							{
								PendingWrite->EncodedSize = PendingWrite->ChunkBuffer.GetSize();
								PendingWrite->ChunkHash = FIoHash::HashBuffer(PendingWrite->ChunkBuffer.GetView());

								if (FIoStatus Status = FIoChunkEncoding::HashBlocks(
									*FIoChunkEncoding::FHeader::Decode(PendingWrite->ChunkHeader.GetView()),
									PendingWrite->ChunkBuffer.GetView(),
									PendingWrite->BlockHashes); !Status.IsOk())
								{
									PendingWrite->ErrorText = FString::Printf(TEXT("Failed to hash encoded blocks '%s'"), *PendingWrite->WriteOptions.FileName);
								}
							}
							else
							{
								PendingWrite->ErrorText = FString::Printf(TEXT("Failed to compress '%s'"), *PendingWrite->WriteOptions.FileName);
							}
						}
						else
						{
							PendingWrite->ErrorText = FString::Printf(TEXT("Failed to read source buffer '%s'"), *PendingWrite->WriteOptions.FileName);
						}
					}, ReadChunkTask);

					UE::Tasks::FTask WriteChunkTask = TaskPipe.Launch(TEXT("WriteChunk"),
						[this, PendingWrite]() mutable
						{
							FIoBuffer ChunkBuffer = MoveTemp(PendingWrite->ChunkBuffer);
							if (ChunkBuffer.GetSize() > 0)
							{
								const FString HashString = LexToString(PendingWrite->ChunkHash);
								const FString FileName = HashString + TEXT(".iochunk");
								const FString FilePath = ChunksDirectory / HashString.Left(2) / FileName;

								if (TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileWriter(*FilePath)); Ar.IsValid())
								{
									const uint64 ChunkSize = ChunkBuffer.GetSize();
									const uint32 CurrentCount = TotalCount.fetch_add(1) + 1; 
									UE_CLOG((CurrentCount % 128 == 0), LogIoStore, Display,
										TEXT("Writing ondemand chunk #%u '%s' -> '%s' (%llu Bytes)"),
										CurrentCount, *PendingWrite->WriteOptions.FileName, *FileName, ChunkSize);

									Ar->Serialize((void*)ChunkBuffer.GetData(), ChunkSize);
								}
								else
								{
									PendingWrite->ErrorText = FString::Printf(TEXT("Failed to create file '%s'"), *FileName);
								}
							}
							else
							{
								PendingWrite->ErrorText = TEXT("Invalid source buffer");
							}

							if (PendingCount.fetch_sub(1, std::memory_order_relaxed) == 1)
							{
								WriteQueueEmptyEvent->Trigger();
							}
							WriteCompletedEvent->Trigger();
						}, EncodeChunkTask);

				break;
			}
		}

		WriteCompletedEvent->Wait();
	}
}

////////////////////////////////////////////////////////////////////////////////
FOnDemandContainer::FOnDemandContainer(FOnDemandIoStoreWriter& OnDemandWriter, const FString& ContainerName, const FIoContainerSettings& ContainerSettings)
	: Writer(OnDemandWriter)
	, Name(ContainerName)
	, Settings(ContainerSettings)
{
}

void FOnDemandContainer::SetReferenceChunkDatabase(TSharedPtr<IIoStoreWriterReferenceChunkDatabase> ReferenceChunkDatabase)
{ }

void FOnDemandContainer::SetHashDatabase(TSharedPtr<IIoStoreWriterHashDatabase> HashDatabase, bool bVerifyHashDatabase)
{ }

void FOnDemandContainer::EnableDiskLayoutOrdering(const TArray<TUniquePtr<FIoStoreReader>>& PatchSourceReaders)
{ }

void FOnDemandContainer::EnumerateChunks(TFunction<bool(FIoStoreTocChunkInfo&&)>&& Callback) const
{ }

void FOnDemandContainer::Append(const FIoChunkId& ChunkId, FIoBuffer Chunk, const FIoWriteOptions& WriteOptions, uint64 OrderHint)
{
	struct FWriteRequest
		: IIoStoreWriteRequest
	{
		FWriteRequest(FIoBuffer InSourceBuffer, uint64 InOrderHint)
			: OrderHint(InOrderHint)
		{
			SourceBuffer = InSourceBuffer;
			SourceBuffer.MakeOwned();
		}

		virtual ~FWriteRequest() = default;

		void PrepareSourceBufferAsync(FGraphEventRef CompletionEvent) override
		{
			CompletionEvent->DispatchSubsequents();
		}

		const FIoBuffer* GetSourceBuffer() override
		{
			return &SourceBuffer;
		}

		void FreeSourceBuffer() override
		{
		}

		uint64 GetOrderHint() override
		{
			return OrderHint;
		}

		TArrayView<const FFileRegion> GetRegions()
		{
			return TArrayView<const FFileRegion>();
		}

		FIoBuffer SourceBuffer;
		uint64 OrderHint;
	};

	Append(ChunkId, new FWriteRequest(Chunk, OrderHint), WriteOptions);
}

void FOnDemandContainer::Append(const FIoChunkId& ChunkId, IIoStoreWriteRequest* Request, const FIoWriteOptions& WriteOptions)
{
	Writer.Append(Name, Settings, ChunkId, Request, WriteOptions);
}

TIoStatusOr<FIoStoreWriterResult> FOnDemandContainer::GetResult() 
{
	return WriteResult;
}

} // namespace UE::IO::Private 

////////////////////////////////////////////////////////////////////////////////
namespace UE
{

TUniquePtr<IOnDemandIoStoreWriter> MakeOnDemandIoStoreWriter(
	const FIoStoreWriterSettings& WriterSettings,
	const FString& OutputDirectory,
	uint32 MaxConcurrentWrites)
{
	return MakeUnique<UE::IO::Private::FOnDemandIoStoreWriter>(WriterSettings, OutputDirectory, MaxConcurrentWrites);
}

} // namespace UE

#endif // (IS_PROGRAM || WITH_EDITOR)
