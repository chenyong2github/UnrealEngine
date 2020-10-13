// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelFormat.h"
#include "Containers/Array.h"
#include "Containers/ArrayView.h"

//
// Describes the way regions of cooked data should be rearranged to achieve higher compression ratios.
//
// Each digit in the pattern description lists the number of bytes to append to the output on each pass over the data range.
// e.g. with Pattern_224, the source data is divided into 8 byte vectors (2 + 2 + 4 = 8). 3 passes are made over the data (one per digit):
//		Pass 1 takes the first 2 bytes of each vector and appends them to the output.
//		Pass 2 takes the next 2 bytes of each vector.
//		Pass 3 takes the remaining 4 bytes of each vector.
//
// Given the example data:
//
//       A0 A1 B0 B1 C0 C1 C2 C3   A2 A3 B2 B3 C4 C5 C6 C7   A4 A5 B4 B5 C8 C9 CA CB
//       --2-- --2-- -----4----- | --2-- --2-- -----4----- | --2-- --2-- -----4-----
//
// And applying the above rules for Pattern_224, the output is:
//
//       A0 A1 A2 A3 A4 A5 B0 B1 B2 B3 B4 B5 C0 C1 C2 C3 C4 C5 C6 C7 C8 C9 CA CB
// Pass: --------1-------- --------2-------- -----------------3-----------------
//
//
// NOTE: Enum values here must match those in AutomationUtils/FileRegions.cs
//
enum class EDataShufflePattern : uint8
{
	None = 0,

	// 8 Byte Vectors
	Pattern_44       = 1,
	Pattern_224      = 2,
	Pattern_116      = 3,
	Pattern_11111111 = 4,

	// 16 Byte Vectors
	Pattern_8224     = 5,
	Pattern_116224   = 6,
	Pattern_116116   = 7,
	Pattern_4444     = 8
};

namespace FDataShuffle
{
	// Selects an appropriate data shuffle pattern for the given pixel format which maximizes data compression.
	inline EDataShufflePattern SelectPattern(EPixelFormat Format)
	{
		switch (Format)
		{
		case PF_DXT1: return EDataShufflePattern::Pattern_224;
		case PF_DXT3: return EDataShufflePattern::Pattern_8224;
		case PF_DXT5: return EDataShufflePattern::Pattern_116224;
		case PF_BC4:  return EDataShufflePattern::Pattern_116;
		case PF_BC5:  return EDataShufflePattern::Pattern_116116;
		}

		return EDataShufflePattern::None;
	}
}

// Represents a region of logically related bytes within a larger block of cooked data.
// Regions are used to improve data compression and patching on some platforms.
struct FFileRegion
{
	static const constexpr TCHAR* RegionsFileExtension = TEXT(".uregs");

	uint64 Offset;
	uint64 Length;
	EDataShufflePattern Pattern;

	FFileRegion()
		: Offset(0)
		, Length(0)
		, Pattern(EDataShufflePattern::None)
	{}

	FFileRegion(uint64 InOffset, uint64 InLength, EDataShufflePattern InPattern)
		: Offset(InOffset)
		, Length(InLength)
		, Pattern(InPattern)
	{}

	static CORE_API void AccumulateFileRegions(TArray<FFileRegion>& InOutRegions, int64 EntryOffset, int64 PayloadOffset, int64 EndOffset, TArrayView<const FFileRegion> InnerFileRegions);
	static CORE_API void SerializeFileRegions(class FArchive& Ar, TArray<FFileRegion>& Regions);
};
