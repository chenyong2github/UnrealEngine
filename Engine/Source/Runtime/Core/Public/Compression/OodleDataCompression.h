// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Logging/LogCategory.h"
#include "Logging/LogMacros.h"

// OodleDataCompression :
//	Unreal API for direct access to Oodle Data lossless data compression
//	for manual encoding (not in Pak/iostore)
//	NOTE : for any data that will be stored to disk, you should not be compressing it yourself!
//		allow the pak/iostore system to choose the compressor!
//
// Let me emphasize again : for data to be stored in packages for shipping games, do NOT use this
//	let the pak/iostore system choose the compression from the config options and platform settings
//
// This is for utility compression in non-shipping game package scenarios
//	eg. uassets, storage caches and back-ends, large network transfers

DECLARE_LOG_CATEGORY_EXTERN(OodleDataCompression, Log, All);

/**
 * EOodleDataCompressor : Choose the Oodle Compressor
 *  this mostly trades decompression speed vs compression ratio
 * 
 * From fastest to slowest (to decode) : Selkie, Mermaid, Kraken, Leviathan
 * 
 * encode speed is determined by EOodleDataCompressionLevel , not the compressor choice.
 * 
 * When in doubt, start with Kraken
 * Representative compression ratios and decode speeds :
 
Selkie4       :  1.86:1 ,   4232.6 dec MB/s
Mermaid4      :  2.21:1 ,   2648.9 dec MB/s
Kraken4       :  2.44:1 ,   1467.1 dec MB/s
Leviathan4    :  2.62:1 ,    961.8 dec MB/s
 * 
 */
//UENUM() // @todo Oodle might be nice if these were UENUM but can't pull UObject from inside Core?
// enum values should not change, they may be persisted
enum class EOodleDataCompressor : uint8
{
	NotSet = 0,
	Selkie = 1,
	Mermaid = 2,
	Kraken  = 3,
	Leviathan = 4
};


/**
 * EOodleDataCompressionLevel : Choose the Oodle Compression Level
 *  this mostly trades encode speed vs compression ratio
 * 
 * If in doubt start with "Normal" (level 4) then move up or down from there
 * 
 * The standard range is in "SuperFast" - "Normal" (levels 1-4)
 * 
 * HyperFast is for real-time encoding with minimal compression
 * 
 * The "Optimal" levels are much slower to encode, but provide more compression
 * they are intended for offline cooks
 * 
 * representative encode speeds with compression ratio trade off :
 
Kraken-4      :  1.55:1 ,  718.3 enc MB/s
Kraken-3      :  1.71:1 ,  541.8 enc MB/s
Kraken-2      :  1.88:1 ,  434.0 enc MB/s
Kraken-1      :  2.10:1 ,  369.1 enc MB/s
Kraken1       :  2.27:1 ,  242.6 enc MB/s
Kraken2       :  2.32:1 ,  157.4 enc MB/s
Kraken3       :  2.39:1 ,   34.8 enc MB/s
Kraken4       :  2.44:1 ,   22.4 enc MB/s
Kraken5       :  2.55:1 ,   10.1 enc MB/s
Kraken6       :  2.56:1 ,    5.4 enc MB/s
Kraken7       :  2.64:1 ,    3.7 enc MB/s

 */
//UENUM() // @todo Oodle might be nice if these were UENUM but can't pull UObject from inside Core?
// EOodleDataCompressionLevel must numerically match the Oodle internal enum values
enum class EOodleDataCompressionLevel : int8
{
	HyperFast4 = -4,
	HyperFast3 = -3,
	HyperFast2 = -2,
	HyperFast1 = -1,
	None = 0,
	SuperFast = 1,
	VeryFast = 2,
	Fast = 3,
	Normal = 4,
	Optimal1 = 5,
	Optimal2 = 6,
	Optimal3 = 7,
	Optimal4 = 8,
};

int64 CORE_API OodleDataCompressedBufferSizeNeeded(int64 IncompressedSize);

int64 CORE_API OodleDataGetMaximumCompressedSize(int64 UncompressedSize);

// OutCompressedSize must be >= OodleDataCompressedBufferSizeNeeded(InUncompressedSize)
// returns compressed size or zero for failure
int64 CORE_API OodleDataCompress(
							void * OutCompressedData, int64 CompressedBufferSize,
							const void * InUncompressedData, int64 UncompressedSize,
							EOodleDataCompressor Compressor,
							EOodleDataCompressionLevel Level);

// OutUncompressedSize is the number of bytes to decompress, it must match the number that you encoded
// InCompressedSize is the buffer size of compressed data, it must be >= the number of compressed bytes needed
bool CORE_API OodleDataDecompress(
						void * OutUncompressedData, int64 UncompressedSize,
						const void * InCompressedData, int64 CompressedSize
						);


// For Compression to/from TArray and such higher level actions use OodleDataCompressionUtil.h

// from Compression.cpp :
void CORE_API OodleDataCompressionFormatInitOnFirstUseFromLock();

// from LaunchEngineLoop :
void CORE_API OodleDataCompressionStartupPreInit();
