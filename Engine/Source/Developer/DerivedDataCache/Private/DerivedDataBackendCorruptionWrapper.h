// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "DerivedDataBackendInterface.h"
#include "HAL/UnrealMemory.h"
#include "Memory/MemoryView.h"
#include "Misc/Crc.h"
#include "Serialization/Archive.h"

namespace UE::DerivedData
{

/**
 * Helper class for placing a footer at the end of of a cache file.
 * No effort is made to byte-swap this as we assume local format.
 */
struct FDerivedDataTrailer
{
	/** Arbitrary number used to identify corruption */
	static constexpr inline uint32 MagicConstant = 0x1e873d89;

	/** Arbitrary number used to identify corruption */
	uint32 Magic = 0;
	/** Version of the trailer */
	uint32 Version = 0;
	/** CRC of the payload, used to detect corruption */
	uint32 CRCofPayload = 0;
	/** Size of the payload, used to detect corruption */
	uint32 SizeOfPayload = 0;

	FDerivedDataTrailer() = default;

	explicit FDerivedDataTrailer(const FMemoryView Data)
		: Magic(MagicConstant)
		, Version(1)
		, CRCofPayload(FCrc::MemCrc_DEPRECATED(Data.GetData(), IntCastChecked<int32>(Data.GetSize())))
		, SizeOfPayload(uint32(Data.GetSize()))
	{
	}

	bool operator==(const FDerivedDataTrailer& Other) const
	{
		return Magic == Other.Magic
			&& Version == Other.Version
			&& CRCofPayload == Other.CRCofPayload
			&& SizeOfPayload == Other.SizeOfPayload;
	}
};

class FCorruptionWrapper
{
public:
	static bool ReadTrailer(TArray<uint8>& Data, const TCHAR* CacheName, const TCHAR* CacheKey)
	{
		if (Data.Num() < sizeof(FDerivedDataTrailer))
		{
			UE_LOG(LogDerivedDataCache, Display, TEXT("%s: Corrupted file (short), ignoring and deleting %s."), CacheName, CacheKey);
			return false;
		}

		FDerivedDataTrailer Trailer;
		FMemory::Memcpy(&Trailer, &Data[Data.Num() - sizeof(FDerivedDataTrailer)], sizeof(FDerivedDataTrailer));
		Data.RemoveAt(Data.Num() - sizeof(FDerivedDataTrailer), sizeof(FDerivedDataTrailer), /*bAllowShrinking*/ false);
		FDerivedDataTrailer RecomputedTrailer(MakeMemoryView(Data));
		if (!(Trailer == RecomputedTrailer))
		{
			UE_LOG(LogDerivedDataCache, Display, TEXT("%s: Corrupted file, ignoring and deleting %s."), CacheName, CacheKey);
			return false;
		}

		return true;
	}

	static void WriteTrailer(FArchive& Ar, TConstArrayView<uint8> Data, const TCHAR* CacheName, const TCHAR* CacheKey)
	{
		checkf(Ar.TotalSize() + sizeof(FDerivedDataTrailer) <= MAX_int32,
			TEXT("%s: Appending the trailer makes the data exceed 2 GiB for %s"), CacheName, CacheKey);
		FDerivedDataTrailer Trailer(MakeMemoryView(Data));
		Ar.Serialize(&Trailer, sizeof(FDerivedDataTrailer));
	}
};

} // UE::DerivedData
