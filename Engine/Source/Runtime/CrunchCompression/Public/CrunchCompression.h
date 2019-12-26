// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#ifndef WITH_CRUNCH
#define WITH_CRUNCH 1
#endif

/** Crunch compression lib is currently only built for Windows */
#define WITH_CRUNCH_COMPRESSION (WITH_CRUNCH && WITH_EDITOR && PLATFORM_WINDOWS)

#if WITH_CRUNCH

#if WITH_CRUNCH_COMPRESSION
/** Crunch compression */
struct FCrunchEncodeParameters
{
	TArray<TArray<uint32>> RawImagesRGBA;
	FName OutputFormat;
	float CompressionAmmount = 0.0f; // 0 min compression, 1 max compression
	uint32 ImageWidth = 0u;
	uint32 ImageHeight = 0u;
	uint32 NumWorkerThreads = 0u;
	bool bIsGammaCorrected = false;
};

namespace CrunchCompression
{
	CRUNCHCOMPRESSION_API bool IsValidFormat(const FName& Format);
	CRUNCHCOMPRESSION_API bool Encode(const FCrunchEncodeParameters& Parameters, TArray<uint8>& OutCodecPayload, TArray<TArray<uint8>>& OutTilePayload);
}
#endif // WITH_CRUNCH_COMPRESSION

/** Decompression */
namespace CrunchCompression
{
	CRUNCHCOMPRESSION_API void* InitializeDecoderContext(const void* HeaderData, size_t HeaderDataSize);
	CRUNCHCOMPRESSION_API bool Decode(void* Context, const void* CompressedPixelData, uint32 Slice, void* OutUncompressedData, size_t OutDataSize, size_t OutUncompressedDataPitch);
	CRUNCHCOMPRESSION_API void DestroyDecoderContext(void* Context);
}

#endif // WITH_CRUNCH
