// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityTypes.h"
#include "StructUtilsTypes.h"

uint32 FMassArchetypeFragmentsInitialValues::CalculateHash() const
{
	TArray<uint32, TInlineAllocator<16>> FragmentHashes;
	FragmentHashes.Reserve(Fragments.Num() + ChunkFragments.Num() + ConstSharedFragments.Num() + SharedFragments.Num());

	// max@todo: Fragments and chunk fragments are not part of the uniqueness and should be removed from the initial values, also should think of a better name for this struct.
	for (const FConstSharedStruct& Fragment : ConstSharedFragments)
	{
		FragmentHashes.Add(PointerHash(Fragment.GetMemory()));
	}

	for (const FSharedStruct& Fragment : SharedFragments)
	{
		FragmentHashes.Add(PointerHash(Fragment.GetMemory()));
	}

	FragmentHashes.Sort();

	uint32 CalculatedHash = 0;
	for (const uint32 FragmentHash : FragmentHashes)
	{
		CalculatedHash = HashCombine(CalculatedHash, FragmentHash);
	}

	return CalculatedHash;
}
