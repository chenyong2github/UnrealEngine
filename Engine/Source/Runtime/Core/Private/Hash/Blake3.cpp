// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hash/Blake3.h"

#include "blake3.h"

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
	FBlake3Hash Hash;
	FBlake3Hash::ByteArray& Output = Hash.GetBytes();
	static_assert(sizeof(decltype(Output)) == BLAKE3_OUT_LEN, "Mismatch in BLAKE3 hash size.");
	const blake3_hasher& Hasher = reinterpret_cast<const blake3_hasher&>(HasherBytes);
	blake3_hasher_finalize(&Hasher, Output, BLAKE3_OUT_LEN);
	return Hash;
}

FBlake3Hash FBlake3::HashBuffer(const void* Data, uint64 Size)
{
	FBlake3 Hash;
	Hash.Update(Data, Size);
	return Hash.Finalize();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
