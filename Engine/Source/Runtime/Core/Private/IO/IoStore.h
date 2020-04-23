// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IO/IoDispatcher.h"

/**
 * I/O Store TOC header.
 */
struct FIoStoreTocHeader
{
	static constexpr char TocMagicImg[] = "-==--==--==--==-";

	uint8	TocMagic[16];
	uint32	TocHeaderSize;
	uint32	TocEntryCount;
	uint32	TocEntrySize;	// For sanity checking
	uint32	TocPartialEntryCount;
	uint32	TocPartialEntrySize;	// For sanity checking
	uint32	TocCompressedBlockEntryCount;
	uint32	TocCompressedBlockEntrySize;	// For sanity checking
	uint32	CompressionMethodNameCount;
	uint32	CompressionMethodNameLength;
	uint32	CompressionBlockSize;
	FIoContainerId ContainerId;
	uint8 Pad[70];

	void MakeMagic()
	{
		FMemory::Memcpy(TocMagic, TocMagicImg, sizeof TocMagic);
	}

	bool CheckMagic() const
	{
		return FMemory::Memcmp(TocMagic, TocMagicImg, sizeof TocMagic) == 0;
	}
};

/**
 * Combined offset and length.
 */
struct FIoOffsetAndLength
{
public:
	inline uint64 GetOffset() const
	{
		return OffsetAndLength[4]
			| (uint64(OffsetAndLength[3]) << 8)
			| (uint64(OffsetAndLength[2]) << 16)
			| (uint64(OffsetAndLength[1]) << 24)
			| (uint64(OffsetAndLength[0]) << 32)
			;
	}

	inline uint64 GetLength() const
	{
		return OffsetAndLength[9]
			| (uint64(OffsetAndLength[8]) << 8)
			| (uint64(OffsetAndLength[7]) << 16)
			| (uint64(OffsetAndLength[6]) << 24)
			| (uint64(OffsetAndLength[5]) << 32)
			;
	}

	inline void SetOffset(uint64 Offset)
	{
		OffsetAndLength[0] = uint8(Offset >> 32);
		OffsetAndLength[1] = uint8(Offset >> 24);
		OffsetAndLength[2] = uint8(Offset >> 16);
		OffsetAndLength[3] = uint8(Offset >>  8);
		OffsetAndLength[4] = uint8(Offset >>  0);
	}

	inline void SetLength(uint64 Length)
	{
		OffsetAndLength[5] = uint8(Length >> 32);
		OffsetAndLength[6] = uint8(Length >> 24);
		OffsetAndLength[7] = uint8(Length >> 16);
		OffsetAndLength[8] = uint8(Length >> 8);
		OffsetAndLength[9] = uint8(Length >> 0);
	}

private:
	// We use 5 bytes for offset and size, this is enough to represent
	// an offset and size of 1PB
	uint8 OffsetAndLength[5 + 5];
};

/**
 * I/O Store TOC entry.
 */
struct FIoStoreTocEntry
{
	FIoOffsetAndLength OffsetAndLength;
	FIoChunkId ChunkId;
	FIoChunkHash ChunkHash;
	
	inline uint64 GetOffset() const
	{
		return OffsetAndLength.GetOffset();
	}

	inline uint64 GetLength() const
	{
		return OffsetAndLength.GetLength();
	}

	inline void SetOffset(uint64 Offset)
	{
		OffsetAndLength.SetOffset(Offset);
	}

	inline void SetLength(uint64 Length)
	{
		OffsetAndLength.SetLength(Length);
	}
};

struct FIoStoreTocPartialEntry
{
	FIoOffsetAndLength OffsetAndLength;
	FIoChunkId ChunkId;
	FIoChunkId OriginalChunkId;

	inline uint64 GetOffset() const
	{
		return OffsetAndLength.GetOffset();
	}

	inline uint64 GetLength() const
	{
		return OffsetAndLength.GetLength();
	}

	inline void SetOffset(uint64 Offset)
	{
		OffsetAndLength.SetOffset(Offset);
	}

	inline void SetLength(uint64 Length)
	{
		OffsetAndLength.SetLength(Length);
	}
};

/**
 * Compression block entry.
 */
struct FIoStoreTocCompressedBlockEntry
{
	/** Offset and size of the compressed block. */
	FIoOffsetAndLength OffsetAndLength;
	/** Index into the compression methods array. */
	uint8 CompressionMethodIndex;
};

class FIoStoreTocReader
{
public:
	UE_NODISCARD FIoStatus Initialize(const TCHAR* TocFilePath);
	
	const FIoStoreTocEntry* GetEntries(uint32& OutCount) const
	{
		OutCount = EntryCount;
		return EntryCount > 0 ? Entries : nullptr;
	}

	const FIoStoreTocPartialEntry* GetPartialEntries(uint32& OutCount) const
	{
		OutCount = PartialEntryCount;
		return PartialEntryCount > 0 ? PartialEntries : nullptr;
	}

	const FIoStoreTocCompressedBlockEntry* GetCompressedBlockEntries(uint32& OutCount) const
	{
		OutCount = CompressedBlockEntryCount;
		return CompressedBlockEntryCount > 0 ? CompressedBlockEntries : nullptr;
	}

	const FName* GetCompressionMethodNames(uint32& OutCount) const
	{
		OutCount = CompressionMethodNames.Num();
		return CompressionMethodNames.Num() > 0 ? CompressionMethodNames.GetData() : nullptr;
	}

	uint32 GetCompressionBlockSize() const
	{
		return Header->CompressionBlockSize;
	}

	FIoContainerId GetContainerId() const
	{
		return Header->ContainerId;
	}

private:
	TArray<FName> CompressionMethodNames;
	TUniquePtr<uint8[]> TocBuffer;
	const FIoStoreTocHeader* Header = nullptr;
	const FIoStoreTocEntry* Entries = nullptr;
	const FIoStoreTocPartialEntry* PartialEntries = nullptr;
	const FIoStoreTocCompressedBlockEntry* CompressedBlockEntries = nullptr;
	uint32 EntryCount = 0;
	uint32 PartialEntryCount = 0;
	uint32 CompressedBlockEntryCount = 0;
};

