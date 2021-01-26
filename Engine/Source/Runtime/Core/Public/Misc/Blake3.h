// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringFwd.h"
#include "HAL/UnrealMemory.h"
#include "Memory/MemoryView.h"
#include "String/BytesToHex.h"
#include "Templates/TypeCompatibleBytes.h"

class FArchive;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Stores a BLAKE3 hash. */
struct FBlake3Hash
{
public:
	using ByteArray = uint8[32];

	/** Construct a zero hash. */
	FBlake3Hash() = default;
	explicit FBlake3Hash(const ByteArray& Hash);

	/** Construct a hash from a 64-character hex string. */
	CORE_API explicit FBlake3Hash(FAnsiStringView HexHash);
	CORE_API explicit FBlake3Hash(FWideStringView HexHash);

	CORE_API friend FArchive& operator<<(FArchive& Ar, FBlake3Hash& Hash);

	/** Reset this to a zero hash. */
	inline void Reset() { *this = FBlake3Hash(); }

	/** Returns a reference to the raw byte array for the hash. */
	inline const ByteArray& GetBytes() const { return Hash; }

	/** Returns whether this is a zero hash. */
	inline bool IsZero() const;

private:
	alignas(uint32) ByteArray Hash{};
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Calculates a BLAKE3 hash. */
class FBlake3
{
public:
	inline FBlake3() { Reset(); }

	FBlake3(const FBlake3&) = delete;
	FBlake3& operator=(const FBlake3&) = delete;

	/** Reset to the default state in which no input has been written. */
	CORE_API void Reset();

	/** Add the data as input to the hash. May be called any number of times. */
	CORE_API void Update(const void* Data, uint64 Size);

	/** Add the view as input to the hash. May be called any number of times. */
	inline void Update(FMemoryView View)
	{
		Update(View.GetData(), View.GetSize());
	}

	/** Finalize the hash of the input data. May be called any number of times, and more input may be added after. */
	CORE_API FBlake3Hash Finalize() const;

	/** Calculate the hash of the input data. */
	CORE_API static FBlake3Hash HashBuffer(const void* Data, uint64 Size);

	/** Calculate the hash of the input view. */
	static inline FBlake3Hash HashBuffer(FMemoryView View)
	{
		return HashBuffer(View.GetData(), View.GetSize());
	}

private:
	TAlignedBytes<1912, 8> HasherBytes;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline FBlake3Hash::FBlake3Hash(const ByteArray& InHash)
{
	FMemory::Memcpy(Hash, InHash, sizeof(ByteArray));
}

inline bool FBlake3Hash::IsZero() const
{
	using UInt32Array = uint32[8];
	static_assert(sizeof(UInt32Array) == sizeof(ByteArray), "Invalid size for UInt32Array");
	for (uint32 Value : reinterpret_cast<const UInt32Array&>(Hash))
	{
		if (Value != 0)
		{
			return false;
		}
	}
	return true;
}

inline bool operator==(const FBlake3Hash& A, const FBlake3Hash& B)
{
	return FMemory::Memcmp(A.GetBytes(), B.GetBytes(), sizeof(decltype(A.GetBytes()))) == 0;
}

inline bool operator!=(const FBlake3Hash& A, const FBlake3Hash& B)
{
	return FMemory::Memcmp(A.GetBytes(), B.GetBytes(), sizeof(decltype(A.GetBytes()))) != 0;
}

inline bool operator<(const FBlake3Hash& A, const FBlake3Hash& B)
{
	return FMemory::Memcmp(A.GetBytes(), B.GetBytes(), sizeof(decltype(A.GetBytes()))) < 0;
}

inline uint32 GetTypeHash(const FBlake3Hash& Hash)
{
	return *reinterpret_cast<const uint32*>(Hash.GetBytes());
}

inline FAnsiStringBuilderBase& operator<<(FAnsiStringBuilderBase& Builder, const FBlake3Hash& Hash)
{
	UE::String::BytesToHexLower(Hash.GetBytes(), Builder);
	return Builder;
}

inline FWideStringBuilderBase& operator<<(FWideStringBuilderBase& Builder, const FBlake3Hash& Hash)
{
	UE::String::BytesToHexLower(Hash.GetBytes(), Builder);
	return Builder;
}
