// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "IO/IoStore.h"
#include "Containers/Map.h"
#include "HAL/FileManager.h"
#include "Templates/UniquePtr.h"

//////////////////////////////////////////////////////////////////////////

constexpr char FIoStoreTocHeader::TocMagicImg[];
constexpr uint32 IoChunkAlignment = 16;

//////////////////////////////////////////////////////////////////////////

FIoStoreEnvironment::FIoStoreEnvironment()
{
}

FIoStoreEnvironment::FIoStoreEnvironment(const FIoStoreEnvironment& BaseEnvironment, FStringView InPartitionName)
{
	BasePath = BaseEnvironment.BasePath;
	PartitionName = InPartitionName;
}

FIoStoreEnvironment::~FIoStoreEnvironment()
{
}

void FIoStoreEnvironment::InitializeFileEnvironment(FStringView InBasePath)
{
	BasePath = InBasePath;
}

//////////////////////////////////////////////////////////////////////////

class FIoStoreWriterImpl
{
public:
	FIoStoreWriterImpl(FIoStoreEnvironment& InEnvironment)
	:	Environment(InEnvironment)
	{
	}

	FIoStatus Initialize()
	{
		IPlatformFile& Ipf = IPlatformFile::GetPlatformPhysical();

		FString PartitionName = Environment.GetPartitionName();
		if (PartitionName.IsEmpty())
		{
			PartitionName = TEXT("global");
		}
		FString TocFilePath = Environment.GetBasePath() / PartitionName + TEXT(".utoc");
		FString ContainerFilePath = Environment.GetBasePath() / PartitionName + TEXT(".ucas");

		Ipf.CreateDirectoryTree(*Environment.GetBasePath());

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

		return FIoStatus::Ok;
	}

	UE_NODISCARD FIoStatus Append(FIoChunkId ChunkId, FIoBuffer Chunk)
	{
		if (!ContainerFileHandle)
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

		FIoStoreTocEntry TocEntry;

		check(ContainerFileHandle->Tell() % IoChunkAlignment == 0);

		TocEntry.SetOffset(ContainerFileHandle->Tell());
		TocEntry.SetLength(Chunk.DataSize());
		TocEntry.ChunkId = ChunkId;

		IsMetadataDirty = true;

		bool Success = ContainerFileHandle->Write(Chunk.Data(), Chunk.DataSize());

		if (uint32 UnpaddedBytes = Chunk.DataSize() % IoChunkAlignment)
		{
			static constexpr uint8 Zeroes[IoChunkAlignment] = {};
			uint32 Padding = (IoChunkAlignment - UnpaddedBytes) % IoChunkAlignment;
			Success &= ContainerFileHandle->Write(Zeroes, Padding);
		}

		if (Success)
		{
			Toc.Add(ChunkId, TocEntry);

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

	UE_NODISCARD FIoStatus FlushMetadata()
	{
		TocFileHandle->Seek(0);

		FIoStoreTocHeader TocHeader;
		FMemory::Memset(TocHeader, 0);

		TocHeader.MakeMagic();
		TocHeader.TocHeaderSize = sizeof TocHeader;
		TocHeader.TocEntryCount = Toc.Num();
		TocHeader.TocEntrySize = sizeof(FIoStoreTocEntry);

		const bool Success = TocFileHandle->Write(reinterpret_cast<const uint8*>(&TocHeader), sizeof TocHeader);

		if (!Success)
		{
			return FIoStatus(EIoErrorCode::WriteError, TEXT("TOC write failed"));
		}

		for (auto& _: Toc)
		{
			FIoStoreTocEntry& TocEntry = _.Value;
			
			TocFileHandle->Write(reinterpret_cast<const uint8*>(&TocEntry), sizeof TocEntry);
		}

		return FIoStatus::Ok;
	}

private:
	FIoStoreEnvironment&				Environment;
	TMap<FIoChunkId, FIoStoreTocEntry>	Toc;
	TUniquePtr<IFileHandle>				ContainerFileHandle;
	TUniquePtr<IFileHandle>				TocFileHandle;
	bool								IsMetadataDirty = true;
};

FIoStoreWriter::FIoStoreWriter(FIoStoreEnvironment& InEnvironment)
:	Impl(new FIoStoreWriterImpl(InEnvironment))
{
}

FIoStoreWriter::~FIoStoreWriter()
{
	Impl->FlushMetadata();
}

FIoStatus FIoStoreWriter::Initialize()
{
	return Impl->Initialize();
}

FIoStatus FIoStoreWriter::Append(FIoChunkId ChunkId, FIoBuffer Chunk)
{
	return Impl->Append(ChunkId, Chunk);
}

FIoStatus FIoStoreWriter::MapPartialRange(FIoChunkId OriginalChunkId, uint64 Offset, uint64 Length, FIoChunkId ChunkIdPartialRange)
{
	return Impl->MapPartialRange(OriginalChunkId, Offset, Length, ChunkIdPartialRange);
}

FIoStatus FIoStoreWriter::FlushMetadata()
{
	return Impl->FlushMetadata();
}
