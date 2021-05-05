// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Memory/CompositeBuffer.h"
#include "Memory/MemoryFwd.h"

class FArchive;
class FName;
struct FBlake3Hash;

/**
 * A compressed buffer stores compressed data in a self-contained format.
 *
 * A buffer is self-contained in the sense that it can be decompressed without external knowledge
 * of the compression format or the size of the raw data.
 */
class FCompressedBuffer
{
public:
	/**
	 * Compress the buffer using the requested compression format.
	 *
	 * @param FormatName   One of NAME_None, NAME_Default, NAME_LZ4.
	 * @param RawData      Raw data to compress. NAME_None will reference owned raw data.
	 * @return An owned compressed buffer, or null on error.
	 */
	[[nodiscard]] CORE_API static FCompressedBuffer Compress(FName FormatName, const FCompositeBuffer& RawData);
	[[nodiscard]] CORE_API static FCompressedBuffer Compress(FName FormatName, const FSharedBuffer& RawData);

	/**
	 * Construct from a compressed buffer previously created by Compress().
	 *
	 * @return A compressed buffer, or null on error, such as an invalid format or corrupt header.
	 */
	[[nodiscard]] CORE_API static FCompressedBuffer FromCompressed(const FCompositeBuffer& CompressedData);
	[[nodiscard]] CORE_API static FCompressedBuffer FromCompressed(FCompositeBuffer&& CompressedData);
	[[nodiscard]] CORE_API static FCompressedBuffer FromCompressed(const FSharedBuffer& CompressedData);
	[[nodiscard]] CORE_API static FCompressedBuffer FromCompressed(FSharedBuffer&& CompressedData);
	[[nodiscard]] CORE_API static FCompressedBuffer FromCompressed(FArchive& Ar);

	/** Reset this to null. */
	inline void Reset() { CompressedData.Reset(); }

	/** Returns true if the compressed buffer is not null. */
	[[nodiscard]] inline explicit operator bool() const { return !IsNull(); }

	/** Returns true if the compressed buffer is null. */
	[[nodiscard]] inline bool IsNull() const { return CompressedData.IsNull(); }

	/** Returns true if the composite buffer is owned. */
	[[nodiscard]] inline bool IsOwned() const { return CompressedData.IsOwned(); }

	/** Returns a copy of the compressed buffer that owns its underlying memory. */
	[[nodiscard]] inline FCompressedBuffer MakeOwned() const & { return FromCompressed(CompressedData.MakeOwned()); }
	[[nodiscard]] inline FCompressedBuffer MakeOwned() && { return FromCompressed(MoveTemp(CompressedData).MakeOwned()); }

	/** Returns a composite buffer containing the compressed data. May be null. May not be owned. */
	[[nodiscard]] inline const FCompositeBuffer& GetCompressed() const & { return CompressedData; }
	[[nodiscard]] inline FCompositeBuffer GetCompressed() && { return MoveTemp(CompressedData); }

	/** Returns the size of the compressed data. Zero if this is null. */
	[[nodiscard]] inline uint64 GetCompressedSize() const { return CompressedData.GetSize(); }

	/** Returns the size of the raw data. Zero on error or if this is empty or null. */
	[[nodiscard]] CORE_API uint64 GetRawSize() const;

	/** Returns the hash of the raw data. Zero on error or if this is null. */
	[[nodiscard]] CORE_API FBlake3Hash GetRawHash() const;

	/**
	 * Returns the name of the compression format used by this buffer.
	 *
	 * The format name may differ from the format name specified when creating the compressed buffer
	 * because an incompressible buffer is stored with NAME_None, and a request of NAME_Default will
	 * be stored in a specific format such as NAME_LZ4.
	 *
	 * @return The format name, or NAME_None if this null, or NAME_Error if the format is unknown.
	 */
	[[nodiscard]] CORE_API FName GetFormatName() const;

	/**
	 * Decompress into a memory view that is exactly GetRawSize() bytes.
	 */
	[[nodiscard]] CORE_API bool TryDecompressTo(FMutableMemoryView RawView) const;

	/**
	 * Decompress into an owned buffer.
	 *
	 * @return An owned buffer containing the raw data, or null on error.
	 */
	[[nodiscard]] CORE_API FSharedBuffer Decompress() const;

	/**
	 * Decompress into an owned composite buffer.
	 *
	 * @return An owned buffer containing the raw data, or null on error.
	 */
	[[nodiscard]] CORE_API FCompositeBuffer DecompressToComposite() const;

private:
	FCompositeBuffer CompressedData;
};

CORE_API FArchive& operator<<(FArchive& Ar, FCompressedBuffer& Buffer);
