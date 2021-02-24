// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Memory/SharedBuffer.h"

/**
 * A compressed buffer stores data in a self-contained format.
 *
 * A buffer is self-contained in the sense that it can be decompressed without external knowledge
 * of the compression format or the size of the uncompressed data.
 */
class FCompressedBuffer
{
public:
	/** Construct a null compressed buffer. */
	FCompressedBuffer() = default;

	/** Construct from a compressed shared buffer. */
	inline explicit FCompressedBuffer(FSharedBuffer InCompressed)
		: Compressed(MoveTemp(InCompressed))
	{
	}

	/** Returns true if the compressed buffer is not null. */
	inline explicit operator bool() const { return !IsNull(); }

	/** Returns true if the compressed buffer is null. */
	inline bool IsNull() const { return Compressed.IsNull(); }

	/** Returns the shared buffer containing the compressed data. May be null. */
	inline FSharedBuffer GetCompressed() const & { return Compressed; }
	inline FSharedBuffer GetCompressed() && { return MoveTemp(Compressed); }

	/** Returns the size of the compressed data. */
	inline uint64 GetCompressedSize() const { return Compressed.GetSize(); }

	/** Returns an owned shared buffer containing the uncompresesd data. May be null. */
	inline FSharedBuffer Decompress() const { return Decompress(*this); }

	/** Compress the buffer using default compression settings. Returns null if input is null. */
	CORE_API static FCompressedBuffer Compress(const FSharedBuffer& Buffer);

	/** Decompress the compressed buffer. Returns null if input is null. */
	CORE_API static FSharedBuffer Decompress(const FCompressedBuffer& Buffer);

private:
	FSharedBuffer Compressed;
};
