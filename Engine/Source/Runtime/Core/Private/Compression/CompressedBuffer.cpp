// Copyright Epic Games, Inc. All Rights Reserved.

#include "Compression/CompressedBuffer.h"

#include "Algo/ForEach.h"
#include "Compression/OodleDataCompression.h"
#include "Hash/Blake3.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/ByteSwap.h"
#include "Misc/Crc.h"
#include "Serialization/Archive.h"
#include "Templates/Function.h"
#include "UObject/NameTypes.h"

THIRD_PARTY_INCLUDES_START
#include "Compression/lz4.h"
THIRD_PARTY_INCLUDES_END

namespace UE::CompressedBuffer
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static constexpr uint64 DefaultBlockSize = 256 * 1024;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Method used to compress the data in a compressed buffer. */
enum class EMethod : uint8
{
	/** Header is followed by one uncompressed block. */
	None = 0,
	/** Header is followed by an array of compressed block sizes then the compressed blocks. */
	Oodle = 3,
	/** Header is followed by an array of compressed block sizes then the compressed blocks. */
	LZ4 = 4,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Header used on every compressed buffer. Always stored in big-endian format. */
struct FHeader
{
	static constexpr uint32 ExpectedMagic = 0xb7756362; // <dot>ucb

	/** A magic number to identify a compressed buffer. Always 0xb7756362. */
	uint32 Magic = ExpectedMagic;
	/** A CRC-32 used to check integrity of the buffer. Uses the polynomial 0x04c11db7. */
	uint32 Crc32 = 0;
	/** The method used to compress the buffer. Affects layout of data following the header. */
	EMethod Method = EMethod::None;
	/** The method-specific compressor used to compress the buffer. */
	uint8 Compressor = 0;
	/** The method-specific compression level used to compress the buffer. */
	uint8 CompressionLevel = 0;
	/** The power of two size of every uncompressed block except the last. Size is 1 << BlockSizeExponent. */
	uint8 BlockSizeExponent = 0;
	/** The number of blocks that follow the header. */
	uint32 BlockCount = 0;
	/** The total size of the uncompressed data. */
	uint64 TotalRawSize = 0;
	/** The total size of the compressed data including the header. */
	uint64 TotalCompressedSize = 0;
	/** The hash of the uncompressed data. */
	FBlake3Hash RawHash;

	/** Checks validity of the buffer based on the magic number, method, and CRC-32. */
	static bool IsValid(const FCompositeBuffer& CompressedData);
	static bool IsValid(const FSharedBuffer& CompressedData) { return IsValid(FCompositeBuffer(CompressedData)); }

	/** Read a header from a buffer that is at least sizeof(FHeader) without any validation. */
	static FHeader Read(const FCompositeBuffer& CompressedData)
	{
		FHeader Header;
		if (sizeof(FHeader) <= CompressedData.GetSize())
		{
			CompressedData.CopyTo(MakeMemoryView(&Header, &Header + 1));
			Header.ByteSwap();
		}
		return Header;
	}

	/**
	 * Write a header to a memory view that is at least sizeof(FHeader).
	 *
	 * @param HeaderView   View of the header to write, including any method-specific header data.
	 */
	void Write(FMutableMemoryView HeaderView) const
	{
		FHeader Header = *this;
		Header.ByteSwap();
		HeaderView.CopyFrom(MakeMemoryView(&Header, &Header + 1));
		Header.ByteSwap();
		Header.Crc32 = CalculateCrc32(HeaderView);
		Header.ByteSwap();
		HeaderView.CopyFrom(MakeMemoryView(&Header, &Header + 1));
	}

	/** Calculate the CRC-32 from a view of a header including any method-specific header data. */
	static uint32 CalculateCrc32(FMemoryView HeaderView)
	{
		uint32 Crc32 = 0;
		constexpr uint64 MethodOffset = STRUCT_OFFSET(FHeader, Method);
		for (FMemoryView View = HeaderView + MethodOffset; const uint64 ViewSize = View.GetSize();)
		{
			const int32 Size = static_cast<int32>(FMath::Min<uint64>(ViewSize, MAX_int32));
			Crc32 = FCrc::MemCrc32(View.GetData(), Size, Crc32);
			View += Size;
		}
		return Crc32;
	}

	void ByteSwap()
	{
		Magic = NETWORK_ORDER32(Magic);
		Crc32 = NETWORK_ORDER32(Crc32);
		BlockCount = NETWORK_ORDER32(BlockCount);
		TotalRawSize = NETWORK_ORDER64(TotalRawSize);
		TotalCompressedSize = NETWORK_ORDER64(TotalCompressedSize);
	}
};

static_assert(sizeof(FHeader) == 64, "FHeader is the wrong size.");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FEncoder
{
public:
	virtual FCompositeBuffer Compress(const FCompositeBuffer& RawData, uint64 BlockSize) const = 0;
};

class FDecoder
{
public:
	virtual FCompositeBuffer Decompress(const FHeader& Header, const FCompositeBuffer& CompressedData) const = 0;
	virtual bool TryDecompressTo(const FHeader& Header, const FCompositeBuffer& CompressedData, FMutableMemoryView RawView) const = 0;
	virtual uint64 GetHeaderSize(const FHeader& Header) const = 0;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FNoneEncoder final : public FEncoder
{
public:
	FCompositeBuffer Compress(const FCompositeBuffer& RawData, uint64 BlockSize) const final
	{
		FHeader Header;
		Header.Method = EMethod::None;
		Header.BlockCount = 1;
		Header.TotalRawSize = RawData.GetSize();
		Header.TotalCompressedSize = Header.TotalRawSize + sizeof(FHeader);
		Header.RawHash = FBlake3::HashBuffer(RawData);

		FUniqueBuffer HeaderData = FUniqueBuffer::Alloc(sizeof(FHeader));
		Header.Write(HeaderData);
		return FCompositeBuffer(HeaderData.MoveToShared(), RawData.MakeOwned());
	}
};

class FNoneDecoder final : public FDecoder
{
public:
	FCompositeBuffer Decompress(const FHeader& Header, const FCompositeBuffer& CompressedData) const final
	{
		if (Header.Method == EMethod::None &&
			Header.TotalCompressedSize == CompressedData.GetSize() &&
			Header.TotalCompressedSize == Header.TotalRawSize + sizeof(FHeader))
		{
			return CompressedData.Mid(sizeof(FHeader), Header.TotalRawSize).MakeOwned();
		}
		return FCompositeBuffer();
	}

	bool TryDecompressTo(const FHeader& Header, const FCompositeBuffer& CompressedData, FMutableMemoryView RawView) const final
	{
		if (Header.Method == EMethod::None &&
			Header.TotalRawSize == RawView.GetSize() &&
			Header.TotalCompressedSize == CompressedData.GetSize() &&
			Header.TotalCompressedSize == Header.TotalRawSize + sizeof(FHeader))
		{
			CompressedData.CopyTo(RawView, sizeof(FHeader));
			return true;
		}
		return false;
	}

	uint64 GetHeaderSize(const FHeader& Header) const final
	{
		return sizeof(FHeader);
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FBlockEncoder : public FEncoder
{
public:
	FCompositeBuffer Compress(const FCompositeBuffer& RawData, uint64 BlockSize) const final;

protected:
	virtual EMethod GetMethod() const = 0;
	virtual uint8 GetCompressor() const = 0;
	virtual uint8 GetCompressionLevel() const = 0;
	virtual uint64 CompressBlockBound(uint64 RawSize) const = 0;
	virtual bool CompressBlock(FMutableMemoryView& CompressedData, FMemoryView RawData) const = 0;

private:
	uint64 GetCompressedBlocksBound(uint64 BlockCount, uint64 BlockSize, uint64 RawSize) const
	{
		switch (BlockCount)
		{
		case 0:  return 0;
		case 1:  return CompressBlockBound(RawSize);
		default: return CompressBlockBound(BlockSize) - BlockSize + RawSize;
		}
	}
};

FCompositeBuffer FBlockEncoder::Compress(const FCompositeBuffer& RawData, const uint64 BlockSize) const
{
	checkf(FMath::IsPowerOfTwo(BlockSize) && BlockSize <= MAX_uint32,
		TEXT("BlockSize must be a 32-bit power of two but was %" UINT64_FMT "."), BlockSize);
	const uint64 RawSize = RawData.GetSize();
	FBlake3 RawHash;

	const uint64 BlockCount = FMath::DivideAndRoundUp(RawSize, BlockSize);
	checkf(BlockCount <= MAX_uint32, TEXT("Raw data of size %" UINT64_FMT " with block size %" UINT64_FMT " requires ")
		TEXT("%" UINT64_FMT " blocks, but the limit is %u."), RawSize, BlockSize, BlockCount, MAX_uint32);

	// Allocate the buffer for the header, metadata, and compressed blocks.
	const uint64 MetaSize = BlockCount * sizeof(uint32);
	const uint64 CompressedDataSize = sizeof(FHeader) + MetaSize + GetCompressedBlocksBound(BlockCount, BlockSize, RawSize);
	FUniqueBuffer CompressedData = FUniqueBuffer::Alloc(CompressedDataSize);

	// Compress the raw data in blocks and store the raw data for incompressible blocks.
	TArray64<uint32> CompressedBlockSizes;
	CompressedBlockSizes.Reserve(static_cast<uint32>(BlockCount));
	uint64 CompressedSize = 0;
	{
		FUniqueBuffer RawBlockCopy;
		FMutableMemoryView CompressedBlocksView = CompressedData.GetView() + sizeof(FHeader) + MetaSize;
		for (uint64 RawOffset = 0; RawOffset < RawSize;)
		{
			const uint64 RawBlockSize = FMath::Min(RawSize - RawOffset, BlockSize);
			const FMemoryView RawBlock = RawData.ViewOrCopyRange(RawOffset, RawBlockSize, RawBlockCopy);
			RawHash.Update(RawBlock);

			FMutableMemoryView CompressedBlock = CompressedBlocksView;
			if (!CompressBlock(CompressedBlock, RawBlock))
			{
				return FCompositeBuffer();
			}

			uint64 CompressedBlockSize = CompressedBlock.GetSize();
			if (RawBlockSize <= CompressedBlockSize)
			{
				CompressedBlockSize = RawBlockSize;
				CompressedBlocksView = CompressedBlocksView.CopyFrom(RawBlock);
			}
			else
			{
				CompressedBlocksView += CompressedBlockSize;
			}

			CompressedBlockSizes.Add(static_cast<uint32>(CompressedBlockSize));
			CompressedSize += CompressedBlockSize;
			RawOffset += RawBlockSize;
		}
	}

	// Return an uncompressed buffer if the compressed data is larger than the raw data.
	if (RawSize <= MetaSize + CompressedSize)
	{
		CompressedData.Reset();
		return FNoneEncoder().Compress(RawData, BlockSize);
	}

	// Write the header and calculate the CRC-32.
	Algo::ForEach(CompressedBlockSizes, [](uint32& Size) { Size = NETWORK_ORDER32(Size); });
	CompressedData.GetView().Mid(sizeof(FHeader), MetaSize).CopyFrom(MakeMemoryView(CompressedBlockSizes));

	FHeader Header;
	Header.Method = GetMethod();
	Header.Compressor = GetCompressor();
	Header.CompressionLevel = GetCompressionLevel();
	Header.BlockSizeExponent = static_cast<uint8>(FMath::FloorLog2_64(BlockSize));
	Header.BlockCount = static_cast<uint32>(BlockCount);
	Header.TotalRawSize = RawSize;
	Header.TotalCompressedSize = sizeof(FHeader) + MetaSize + CompressedSize;
	Header.RawHash = RawHash.Finalize();
	Header.Write(CompressedData.GetView().Left(sizeof(FHeader) + MetaSize));

	const FMemoryView CompositeView = CompressedData.GetView().Left(Header.TotalCompressedSize);
	return FCompositeBuffer(FSharedBuffer::MakeView(CompositeView, CompressedData.MoveToShared()));
}

class FBlockDecoder : public FDecoder
{
public:
	FCompositeBuffer Decompress(const FHeader& Header, const FCompositeBuffer& CompressedData) const final;
	bool TryDecompressTo(const FHeader& Header, const FCompositeBuffer& CompressedData, FMutableMemoryView RawView) const final;

	uint64 GetHeaderSize(const FHeader& Header) const final
	{
		return sizeof(FHeader) + sizeof(uint32) * uint64(Header.BlockCount);
	}

protected:
	virtual bool DecompressBlock(FMutableMemoryView RawData, FMemoryView CompressedData) const = 0;
};

FCompositeBuffer FBlockDecoder::Decompress(const FHeader& Header, const FCompositeBuffer& CompressedData) const
{
	if (Header.BlockCount == 0 ||
		Header.TotalCompressedSize != CompressedData.GetSize())
	{
		return FCompositeBuffer();
	}

	// The raw data cannot reference the compressed data unless it is owned.
	// An empty raw buffer requires an empty segment, which this path creates.
	if (!CompressedData.IsOwned() || Header.TotalRawSize == 0)
	{
		FUniqueBuffer Buffer = FUniqueBuffer::Alloc(Header.TotalRawSize);
		return TryDecompressTo(Header, CompressedData, Buffer) ? FCompositeBuffer(Buffer.MoveToShared()) : FCompositeBuffer();
	}

	TArray64<uint32> CompressedBlockSizes;
	CompressedBlockSizes.AddUninitialized(Header.BlockCount);
	CompressedData.CopyTo(MakeMemoryView(CompressedBlockSizes), sizeof(FHeader));
	Algo::ForEach(CompressedBlockSizes, [](uint32& Size) { Size = NETWORK_ORDER32(Size); });

	// Allocate the buffer for the raw blocks that were compressed.
	FSharedBuffer RawData;
	FMutableMemoryView RawDataView;
	const uint64 BlockSize = uint64(1) << Header.BlockSizeExponent;
	{
		uint64 RawDataSize = 0;
		uint64 RemainingRawSize = Header.TotalRawSize;
		for (const uint32 CompressedBlockSize : CompressedBlockSizes)
		{
			const uint64 RawBlockSize = FMath::Min(RemainingRawSize, BlockSize);
			if (CompressedBlockSize < BlockSize)
			{
				RawDataSize += RawBlockSize;
			}
			RemainingRawSize -= RawBlockSize;
		}
		FUniqueBuffer RawDataBuffer = FUniqueBuffer::Alloc(RawDataSize);
		RawDataView = RawDataBuffer;
		RawData = RawDataBuffer.MoveToShared();
	}

	// Decompress the compressed data in blocks and reference the uncompressed blocks.
	uint64 PendingCompressedSegmentOffset = sizeof(FHeader) + uint64(Header.BlockCount) * sizeof(uint32);
	uint64 PendingCompressedSegmentSize = 0;
	uint64 PendingRawSegmentOffset = 0;
	uint64 PendingRawSegmentSize = 0;
	TArray<FSharedBuffer, TInlineAllocator<1>> Segments;

	const auto CommitPendingCompressedSegment = [&PendingCompressedSegmentOffset, &PendingCompressedSegmentSize, &CompressedData, &Segments]
	{
		if (PendingCompressedSegmentSize)
		{
			CompressedData.IterateRange(PendingCompressedSegmentOffset, PendingCompressedSegmentSize,
				[&Segments](FMemoryView View, const FSharedBuffer& ViewOuter)
				{
					Segments.Add(FSharedBuffer::MakeView(View, ViewOuter));
				});
			PendingCompressedSegmentOffset += PendingCompressedSegmentSize;
			PendingCompressedSegmentSize = 0;
		}
	};

	const auto CommitPendingRawSegment = [&PendingRawSegmentOffset, &PendingRawSegmentSize, &RawData, &Segments]
	{
		if (PendingRawSegmentSize)
		{
			const FMemoryView PendingSegment = RawData.GetView().Mid(PendingRawSegmentOffset, PendingRawSegmentSize);
			Segments.Add(FSharedBuffer::MakeView(PendingSegment, RawData));
			PendingRawSegmentOffset += PendingRawSegmentSize;
			PendingRawSegmentSize = 0;
		}
	};

	FUniqueBuffer CompressedBlockCopy;
	uint64 RemainingRawSize = Header.TotalRawSize;
	uint64 RemainingCompressedSize = CompressedData.GetSize();
	for (const uint32 CompressedBlockSize : CompressedBlockSizes)
	{
		if (RemainingCompressedSize < CompressedBlockSize)
		{
			return FCompositeBuffer();
		}

		const uint64 RawBlockSize = FMath::Min(RemainingRawSize, BlockSize);
		if (RawBlockSize == CompressedBlockSize)
		{
			CommitPendingRawSegment();
			PendingCompressedSegmentSize += RawBlockSize;
		}
		else
		{
			CommitPendingCompressedSegment();
			const FMemoryView CompressedBlock = CompressedData.ViewOrCopyRange(PendingCompressedSegmentOffset, CompressedBlockSize, CompressedBlockCopy);
			if (!DecompressBlock(RawDataView.Left(RawBlockSize), CompressedBlock))
			{
				return FCompositeBuffer();
			}
			PendingCompressedSegmentOffset += CompressedBlockSize;
			PendingRawSegmentSize += RawBlockSize;
			RawDataView += RawBlockSize;
		}

		RemainingCompressedSize -= CompressedBlockSize;
		RemainingRawSize -= RawBlockSize;
	}

	CommitPendingCompressedSegment();
	CommitPendingRawSegment();

	return FCompositeBuffer(MoveTemp(Segments));
}

bool FBlockDecoder::TryDecompressTo(const FHeader& Header, const FCompositeBuffer& CompressedData, FMutableMemoryView RawView) const
{
	if (Header.TotalRawSize != RawView.GetSize() ||
		Header.TotalCompressedSize != CompressedData.GetSize())
	{
		return false;
	}

	TArray64<uint32> CompressedBlockSizes;
	CompressedBlockSizes.AddUninitialized(Header.BlockCount);
	CompressedData.CopyTo(MakeMemoryView(CompressedBlockSizes), sizeof(FHeader));
	Algo::ForEach(CompressedBlockSizes, [](uint32& Size) { Size = NETWORK_ORDER32(Size); });

	FUniqueBuffer CompressedBlockCopy;
	const uint64 BlockSize = uint64(1) << Header.BlockSizeExponent;
	uint64 CompressedOffset = sizeof(FHeader) + uint64(Header.BlockCount) * sizeof(uint32);
	uint64 RemainingRawSize = Header.TotalRawSize;
	uint64 RemainingCompressedSize = CompressedData.GetSize();
	for (uint32 CompressedBlockSize : CompressedBlockSizes)
	{
		if (RemainingCompressedSize < CompressedBlockSize)
		{
			return false;
		}

		const uint64 RawBlockSize = FMath::Min(RemainingRawSize, BlockSize);
		if (RawBlockSize == CompressedBlockSize)
		{
			CompressedData.CopyTo(RawView.Left(RawBlockSize), CompressedOffset);
		}
		else
		{
			const FMemoryView CompressedBlock = CompressedData.ViewOrCopyRange(CompressedOffset, CompressedBlockSize, CompressedBlockCopy);
			if (!DecompressBlock(RawView.Left(RawBlockSize), CompressedBlock))
			{
				return false;
			}
		}

		RemainingCompressedSize -= CompressedBlockSize;
		RemainingRawSize -= RawBlockSize;
		CompressedOffset += CompressedBlockSize;
		RawView += RawBlockSize;
	}

	return RemainingRawSize == 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FOodleEncoder final : public FBlockEncoder
{
public:
	FOodleEncoder(FOodleDataCompression::ECompressor InCompressor, FOodleDataCompression::ECompressionLevel InCompressionLevel)
		: Compressor(InCompressor)
		, CompressionLevel(InCompressionLevel)
	{
	}

protected:
	EMethod GetMethod() const final { return EMethod::Oodle; }
	uint8 GetCompressor() const final { return static_cast<uint8>(Compressor); }
	uint8 GetCompressionLevel() const final { return static_cast<uint8>(CompressionLevel); }

	uint64 CompressBlockBound(uint64 RawSize) const final
	{
		return static_cast<uint64>(FOodleDataCompression::CompressedBufferSizeNeeded(static_cast<int64>(RawSize)));
	}

	bool CompressBlock(FMutableMemoryView& CompressedData, FMemoryView RawData) const final
	{
		const int64 Size = FOodleDataCompression::Compress(
			CompressedData.GetData(), static_cast<uint64>(CompressedData.GetSize()),
			RawData.GetData(), static_cast<int64>(RawData.GetSize()),
			Compressor, CompressionLevel);
		CompressedData.LeftInline(static_cast<uint64>(Size));
		return Size > 0;
	}

private:
	const FOodleDataCompression::ECompressor Compressor;
	const FOodleDataCompression::ECompressionLevel CompressionLevel;
};

class FOodleDecoder final : public FBlockDecoder
{
protected:
	bool DecompressBlock(FMutableMemoryView RawData, FMemoryView CompressedData) const final
	{
		return FOodleDataCompression::Decompress(
			RawData.GetData(), static_cast<int64>(RawData.GetSize()),
			CompressedData.GetData(), static_cast<int64>(CompressedData.GetSize()));
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FLZ4Encoder final : public FBlockEncoder
{
protected:
	EMethod GetMethod() const final { return EMethod::LZ4; }
	uint8 GetCompressor() const final { return 0; }
	uint8 GetCompressionLevel() const final { return 0; }

	uint64 CompressBlockBound(uint64 RawSize) const final
	{
		if (RawSize <= LZ4_MAX_INPUT_SIZE)
		{
			return static_cast<uint64>(LZ4_compressBound(static_cast<int>(RawSize)));
		}
		return 0;
	}

	bool CompressBlock(FMutableMemoryView& CompressedData, FMemoryView RawData) const final
	{
		if (RawData.GetSize() <= LZ4_MAX_INPUT_SIZE)
		{
			const int Size = LZ4_compress_default(
				static_cast<const char*>(RawData.GetData()), static_cast<char*>(CompressedData.GetData()),
				static_cast<int>(RawData.GetSize()), static_cast<int>(FMath::Min<uint64>(CompressedData.GetSize(), MAX_int32)));
			CompressedData.LeftInline(static_cast<uint64>(Size));
			return Size > 0;
		}
		return false;
	}
};

class FLZ4Decoder final : public FBlockDecoder
{
protected:
	bool DecompressBlock(FMutableMemoryView RawData, FMemoryView CompressedData) const final
	{
		if (CompressedData.GetSize() <= MAX_int32)
		{
			const int Size = LZ4_decompress_safe(
				static_cast<const char*>(CompressedData.GetData()),
				static_cast<char*>(RawData.GetData()),
				static_cast<int>(CompressedData.GetSize()),
				static_cast<int>(FMath::Min<uint64>(RawData.GetSize(), LZ4_MAX_INPUT_SIZE)));
			return static_cast<uint64>(Size) == RawData.GetSize();
		}
		return false;
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const FDecoder* GetDecoder(EMethod Method)
{
	static FNoneDecoder None;
	static FOodleDecoder Oodle;
	static FLZ4Decoder LZ4;
	switch (Method)
	{
	default:
		return nullptr;
	case EMethod::None:
		return &None;
	case EMethod::Oodle:
		return &Oodle;
	case EMethod::LZ4:
		return &LZ4;
	}
}

static const FEncoder* GetEncoder(FName FormatName)
{
	static FNoneEncoder None;
	static FOodleEncoder Oodle(FOodleDataCompression::ECompressor::Mermaid, FOodleDataCompression::ECompressionLevel::VeryFast);
	static FLZ4Encoder LZ4;
	if (FormatName.IsNone())
	{
		return &None;
	}
	if (FormatName == NAME_Oodle)
	{
		return &Oodle;
	}
	if (FormatName == NAME_LZ4 || FormatName == NAME_Default)
	{
		return &LZ4;
	}
	return nullptr;
}

static FName GetMethodName(EMethod Method)
{
	switch (Method)
	{
	default:
		return NAME_Error;
	case EMethod::None:
		return NAME_None;
	case EMethod::Oodle:
		return NAME_Oodle;
	case EMethod::LZ4:
		return NAME_LZ4;
	}
}

template <typename BufferType>
inline FCompositeBuffer ValidBufferOrEmpty(BufferType&& CompressedData)
{
	return FHeader::IsValid(CompressedData) ? FCompositeBuffer(Forward<BufferType>(CompressedData)) : FCompositeBuffer();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool FHeader::IsValid(const FCompositeBuffer& CompressedData)
{
	if (sizeof(FHeader) <= CompressedData.GetSize())
	{
		const FHeader Header = Read(CompressedData);
		if (Header.Magic == FHeader::ExpectedMagic)
		{
			if (const FDecoder* const Decoder = GetDecoder(Header.Method))
			{
				FUniqueBuffer HeaderCopy;
				const FMemoryView HeaderView = CompressedData.ViewOrCopyRange(0, Decoder->GetHeaderSize(Header), HeaderCopy);
				if (Header.Crc32 == FHeader::CalculateCrc32(HeaderView))
				{
					return true;
				}
			}
		}
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // UE::CompressedBuffer

FCompressedBuffer FCompressedBuffer::Compress(const FCompositeBuffer& RawData)
{
	return Compress(RawData, FOodleDataCompression::ECompressor::Mermaid, FOodleDataCompression::ECompressionLevel::VeryFast);
}

FCompressedBuffer FCompressedBuffer::Compress(const FSharedBuffer& RawData)
{
	return Compress(FCompositeBuffer(RawData));
}

FCompressedBuffer FCompressedBuffer::Compress(
	const FCompositeBuffer& RawData,
	FOodleDataCompression::ECompressor Compressor,
	FOodleDataCompression::ECompressionLevel CompressionLevel,
	uint64 BlockSize)
{
	using namespace UE::CompressedBuffer;

	if (BlockSize == 0)
	{
		BlockSize = DefaultBlockSize;
	}

	FCompressedBuffer Local;
	if (CompressionLevel == FOodleDataCompression::ECompressionLevel::None)
	{
		Local.CompressedData = FNoneEncoder().Compress(RawData, BlockSize);
	}
	else
	{
		Local.CompressedData = FOodleEncoder(Compressor, CompressionLevel).Compress(RawData, BlockSize);
	}
	return Local;
}

FCompressedBuffer FCompressedBuffer::Compress(
	const FSharedBuffer& RawData,
	FOodleDataCompression::ECompressor Compressor,
	FOodleDataCompression::ECompressionLevel CompressionLevel,
	uint64 BlockSize)
{
	return Compress(FCompositeBuffer(RawData), Compressor, CompressionLevel, BlockSize);
}

FCompressedBuffer FCompressedBuffer::Compress(FName FormatName, const FCompositeBuffer& RawData)
{
	using namespace UE::CompressedBuffer;
	FCompressedBuffer Local;
	if (const FEncoder* const Encoder = GetEncoder(FormatName))
	{
		Local.CompressedData = Encoder->Compress(RawData, DefaultBlockSize);
	}
	return Local;
}

FCompressedBuffer FCompressedBuffer::Compress(FName FormatName, const FSharedBuffer& RawData)
{
	return Compress(FormatName, FCompositeBuffer(RawData));
}

FCompressedBuffer FCompressedBuffer::FromCompressed(const FCompositeBuffer& InCompressedData)
{
	FCompressedBuffer Local;
	Local.CompressedData = UE::CompressedBuffer::ValidBufferOrEmpty(InCompressedData);
	return Local;
}

FCompressedBuffer FCompressedBuffer::FromCompressed(FCompositeBuffer&& InCompressedData)
{
	FCompressedBuffer Local;
	Local.CompressedData = UE::CompressedBuffer::ValidBufferOrEmpty(MoveTemp(InCompressedData));
	return Local;
}

FCompressedBuffer FCompressedBuffer::FromCompressed(const FSharedBuffer& InCompressedData)
{
	FCompressedBuffer Local;
	Local.CompressedData = UE::CompressedBuffer::ValidBufferOrEmpty(InCompressedData);
	return Local;
}

FCompressedBuffer FCompressedBuffer::FromCompressed(FSharedBuffer&& InCompressedData)
{
	FCompressedBuffer Local;
	Local.CompressedData = UE::CompressedBuffer::ValidBufferOrEmpty(MoveTemp(InCompressedData));
	return Local;
}

FCompressedBuffer FCompressedBuffer::FromCompressed(FArchive& Ar)
{
	using namespace UE::CompressedBuffer;
	check(Ar.IsLoading());

	if (sizeof(FHeader) > Ar.TotalSize() - Ar.Tell())
	{
		return FCompressedBuffer();
	}

	FHeader Header;
	Ar.Serialize(&Header, sizeof(FHeader));
	Header.ByteSwap();

	if (Header.TotalCompressedSize + Ar.Tell() > Ar.TotalSize() + sizeof(FHeader))
	{
		return FCompressedBuffer();
	}

	FUniqueBuffer MutableBuffer = FUniqueBuffer::Alloc(Header.TotalCompressedSize);
	Header.ByteSwap();
	const FMutableMemoryView MutableView = MutableBuffer.GetView().CopyFrom(MakeMemoryView(&Header, &Header + 1));
	Ar.Serialize(MutableView.GetData(), static_cast<int64>(MutableView.GetSize()));

	return FromCompressed(MutableBuffer.MoveToShared());
}

uint64 FCompressedBuffer::GetRawSize() const
{
	return CompressedData ? UE::CompressedBuffer::FHeader::Read(CompressedData).TotalRawSize : 0;
}

FBlake3Hash FCompressedBuffer::GetRawHash() const
{
	return CompressedData ? UE::CompressedBuffer::FHeader::Read(CompressedData).RawHash : FBlake3Hash();
}

FName FCompressedBuffer::GetFormatName() const
{
	using namespace UE::CompressedBuffer;
	return CompressedData ? GetMethodName(FHeader::Read(CompressedData).Method) : NAME_None;
}

bool FCompressedBuffer::TryGetCompressParameters(
	FOodleDataCompression::ECompressor& OutCompressor,
	FOodleDataCompression::ECompressionLevel& OutCompressionLevel) const
{
	using namespace UE::CompressedBuffer;
	if (CompressedData)
	{
		switch (const FHeader Header = FHeader::Read(CompressedData); Header.Method)
		{
		case EMethod::None:
			OutCompressor = FOodleDataCompression::ECompressor::NotSet;
			OutCompressionLevel = FOodleDataCompression::ECompressionLevel::None;
			return true;
		case EMethod::Oodle:
			OutCompressor = FOodleDataCompression::ECompressor(Header.Compressor);
			OutCompressionLevel = FOodleDataCompression::ECompressionLevel(Header.CompressionLevel);
			return true;
		default:
			break;
		}
	}
	return false;
}

bool FCompressedBuffer::TryDecompressTo(FMutableMemoryView RawView) const
{
	using namespace UE::CompressedBuffer;
	if (CompressedData)
	{
		const FHeader Header = FHeader::Read(CompressedData);
		if (const FDecoder* const Decoder = GetDecoder(Header.Method))
		{
			return Decoder->TryDecompressTo(Header, CompressedData, RawView);
		}
	}
	return false;
}

FSharedBuffer FCompressedBuffer::Decompress() const
{
	using namespace UE::CompressedBuffer;
	if (CompressedData)
	{
		const FHeader Header = FHeader::Read(CompressedData);
		if (const FDecoder* const Decoder = GetDecoder(Header.Method))
		{
			if (Header.Method == EMethod::None)
			{
				return Decoder->Decompress(Header, CompressedData).Flatten();
			}
			FUniqueBuffer RawData = FUniqueBuffer::Alloc(Header.TotalRawSize);
			if (Decoder->TryDecompressTo(Header, CompressedData, RawData))
			{
				return RawData.MoveToShared();
			}
		}
	}
	return FSharedBuffer();
}

FCompositeBuffer FCompressedBuffer::DecompressToComposite() const
{
	using namespace UE::CompressedBuffer;
	if (CompressedData)
	{
		const FHeader Header = FHeader::Read(CompressedData);
		if (const FDecoder* const Decoder = GetDecoder(Header.Method))
		{
			return Decoder->Decompress(Header, CompressedData);
		}
	}
	return FCompositeBuffer();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FArchive& operator<<(FArchive& Ar, FCompressedBuffer& Buffer)
{
	if (Ar.IsLoading())
	{
		Buffer = FCompressedBuffer::FromCompressed(Ar);
	}
	else
	{
		for (const FSharedBuffer& Segment : Buffer.GetCompressed().GetSegments())
		{
			Ar.Serialize(const_cast<void*>(Segment.GetData()), static_cast<int64>(Segment.GetSize()));
		}
	}
	return Ar;
}
