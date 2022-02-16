// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeKernelPermutationVector.h"

#include "ComputeFramework/ComputeKernelPermutationSet.h"

void FComputeKernelPermutationVector::AddPermutation(FString const& Name, uint32 NumValues)
{
	check(NumValues > 0);

	uint32& PackedPermutationBits = Permutations.FindOrAdd(Name);
	if (PackedPermutationBits != 0)
	{
		// Already found.
		FPermutationBits PermutationBits;
		PermutationBits.PackedValue = PackedPermutationBits;
		check(PermutationBits.NumValues == NumValues);
		return;
	}

	FPermutationBits PermutationBits;
	PermutationBits.BitIndex = BitCount;
	PermutationBits.NumValues = NumValues;
	PackedPermutationBits = PermutationBits.PackedValue;

	BitCount += FMath::CeilLogTwo(NumValues);
	check(BitCount <= 32);
}

void FComputeKernelPermutationVector::AddPermutationSet(struct FComputeKernelPermutationSet const& PermutationSet)
{
	for (FComputeKernelPermutationBool const& Permuation : PermutationSet.BooleanOptions)
	{
		AddPermutation(Permuation.Name, 1);
	}
}

void FComputeKernelPermutationId::Set(FString const& Name, uint32 PrecomputedNameHash, uint32 Value)
{
	uint32 const* PackedPermutationBits = PermutationVector.Permutations.FindByHash(PrecomputedNameHash, Name);
	if (ensure(PackedPermutationBits != nullptr))
	{
		FComputeKernelPermutationVector::FPermutationBits PermutationBits;
		PermutationBits.PackedValue = *PackedPermutationBits;
		ensure(Value < PermutationBits.NumValues);
		PermutationId |= (Value << PermutationBits.BitIndex);
	}
}

void FComputeKernelPermutationId::Set(FString const& Name, uint32 Value)
{
	Set(Name, GetTypeHash(Name), Value);
}
