// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FGLTFContainerUtility
{
public:

	static void WriteGlb(FArchive& Archive, const TArray<uint8>& JsonData, const TArray<uint8>& BinaryData);

private:

	static void WriteHeader(FArchive& Archive, uint32 FileSize);
	static void WriteChunk(FArchive& Archive, uint32 ChunkType, const TArray<uint8>& ChunkData, uint8 ChunkTrailingByte);

	static void WriteInt(FArchive& Archive, uint32 Value);
	static void WriteData(FArchive& Archive, const TArray<uint8>& Data);
	static void WriteFill(FArchive& Archive, int32 Size, uint8 Value);

	static int32 GetPaddedChunkSize(int32 Size);
	static int32 GetTrailingChunkSize(int32 Size);
};
