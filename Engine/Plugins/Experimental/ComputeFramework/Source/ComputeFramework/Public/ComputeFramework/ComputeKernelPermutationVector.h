// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ComputeKernelPermutationVector.generated.h"

USTRUCT()
struct FComputeKernelPermutationVector
{
	GENERATED_BODY()

	union FPermutationBits
	{
		uint32 PackedValue = 0;
	
		struct
		{
			uint16 BitIndex;
			uint16 NumValues;
		};
	};

	/** Map from permutation define name to packed FPermutaionBits value. */
	UPROPERTY()
	TMap<FString, uint32> Permutations;
	
	/** Number of permutation bits allocated. */
	UPROPERTY()
	uint32 BitCount = 0;

	/** Add a permutation to the vector. */
	COMPUTEFRAMEWORK_API void AddPermutation(FString const& Name, uint32 NumValues);

	/** Add a permutation to the vector. */
	void AddPermutationSet(struct FComputeKernelPermutationSet const& PermutationSet);
};

/** Helper to accumulate a shader PermutationId by adding permutation values. */
class COMPUTEFRAMEWORK_API FComputeKernelPermutationId
{
public:
	explicit FComputeKernelPermutationId(FComputeKernelPermutationVector const& InPermutationVector)
		: PermutationVector(InPermutationVector)
		, PermutationId(0)
	{}

	/** Set a permutation. */
	void Set(FString const& Name, uint32 Value);
	void Set(FString const& Name, uint32 PrecomputedNameHash, uint32 Value);
	
	/** Get the current accumulated permutation id. */
	uint32 Get() const { return PermutationId; }

private:
	FComputeKernelPermutationVector const& PermutationVector;
	uint32 PermutationId;
};
