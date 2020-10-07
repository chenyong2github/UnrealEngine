// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/Blake3.h"

#include "Containers/StringView.h"
#include "Memory/MemoryView.h"
#include "Serialization/Archive.h"
#include "String/HexToBytes.h"

#include "blake3.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FBlake3Hash::FBlake3Hash(const FAnsiStringView HexHash)
{
	check(HexHash.Len() == sizeof(ByteArray) * 2);
	UE::String::HexToBytes(HexHash, Hash);
}

FBlake3Hash::FBlake3Hash(const FWideStringView HexHash)
{
	check(HexHash.Len() == sizeof(ByteArray) * 2);
	UE::String::HexToBytes(HexHash, Hash);
}

FArchive& operator<<(FArchive& Ar, FBlake3Hash& Hash)
{
	Ar.Serialize(Hash.Hash, sizeof(Hash.Hash));
	return Ar;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static_assert(sizeof(FBlake3) == sizeof(blake3_hasher), "Adjust the allocation in FBlake3 to match blake3_hasher");

void FBlake3::Reset()
{
	blake3_hasher& Hasher = reinterpret_cast<blake3_hasher&>(HasherBytes);
	blake3_hasher_init(&Hasher);
}

void FBlake3::Update(const void* Data, uint64 Size)
{
	blake3_hasher& Hasher = reinterpret_cast<blake3_hasher&>(HasherBytes);
	blake3_hasher_update(&Hasher, Data, Size);
}

FBlake3Hash FBlake3::Finalize() const
{
	uint8 Output[BLAKE3_OUT_LEN];
	const blake3_hasher& Hasher = reinterpret_cast<const blake3_hasher&>(HasherBytes);
	blake3_hasher_finalize(&Hasher, Output, BLAKE3_OUT_LEN);
	return FBlake3Hash(Output);
}

FBlake3Hash FBlake3::HashBuffer(const void* Data, uint64 Size)
{
	FBlake3 Hash;
	Hash.Update(Data, Size);
	return Hash.Finalize();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
