// Copyright Epic Games, Inc. All Rights Reserved.

#include "Compression/CompressedBuffer.h"

FCompressedBuffer FCompressedBuffer::Compress(const FSharedBuffer& Buffer)
{
	// DDC-TODO: Switch to the new compression interface when it is ready. For now, match the "no compression" method.
	FUniqueBuffer CompressedBuffer = FUniqueBuffer::Alloc(Buffer.GetSize() + 1);
	FMemory::Memset(CompressedBuffer.GetData(), 0, 1);
	FMemory::Memcpy(CompressedBuffer.GetView().RightChop(1).GetData(), Buffer.GetData(), Buffer.GetSize());
	return FCompressedBuffer(FSharedBuffer(MoveTemp(CompressedBuffer)));
}

FSharedBuffer FCompressedBuffer::Decompress(const FCompressedBuffer& Buffer)
{
	// DDC-TODO: Switch to the new compression interface when it is ready. For now, match the "no compression" method.
	if (FSharedBuffer Compressed = Buffer.GetCompressed())
	{
		const FMemoryView View = Compressed.GetView() + 1;
		return FSharedBuffer::MakeView(View, MoveTemp(Compressed));
	}
	return FSharedBuffer();
}
