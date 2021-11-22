// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComputeKernelPermutationSet.generated.h"

UENUM()
enum class EComputeKernelPermutationType : uint8
{
	Bool,
	Range,
	Set,
	Enum,

	Count,
};

// #TODO_ZABIR: refactor to a compact bitset once we do the UI work to present it reasonably.
USTRUCT()
struct FComputeKernelPermutationBool
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Permutation Options")
	FString Name;

	UPROPERTY(EditDefaultsOnly, Category = "Permutation Options")
	bool Value;

	FComputeKernelPermutationBool()
		: Value(false)
	{
	}

	explicit FComputeKernelPermutationBool(FString InName, bool InValue = false)
		: Name(MoveTemp(InName))
		, Value(InValue)
	{
	}
};

USTRUCT()
struct FComputeKernelPermutationSet
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, EditFixedSize, meta = (EditFixedOrder), Category = "Permutation Options")
	TArray<FComputeKernelPermutationBool> BooleanOptions;

	uint32 GetPermutationCount() const
	{
		uint32 PermutationCount = (1 << BooleanOptions.Num());

		check(PermutationCount < INT32_MAX);
		return PermutationCount;
	}

	uint32 GetPermutationId() const
	{
		checkSlow(GetPermutationCount());

		uint32 PermutationId = 0;

		for (auto& BoolOpt : BooleanOptions)
		{
			Encode(&PermutationId, 2, BoolOpt.Value ? 1 : 0);
		}

		return PermutationId;
	}

private:
	static void Encode(uint32* Encoded, uint32 ValueRange, uint32 Value)
	{
		*Encoded *= ValueRange;
		*Encoded += Value;
	}

	static uint32 Decode(uint32* Encoded, uint32 ValueRange)
	{
		uint32 Value = *Encoded % ValueRange;
		*Encoded /= ValueRange;

		return Value;
	}
};

USTRUCT()
struct FComputeKernelDefinitions
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category = "Kernel")
	FString Symbol;

	UPROPERTY(EditDefaultsOnly, Category = "Kernel")
	FString Define;

	FComputeKernelDefinitions() = default;

	explicit FComputeKernelDefinitions(FString InSymbol, FString InDefine = FString())
		: Symbol(MoveTemp(InSymbol))
		, Define(MoveTemp(InDefine))
	{
	}
};

USTRUCT()
struct FComputeKernelDefinitionsSet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, EditFixedSize, meta = (EditFixedOrder), Category = "Permutation Options")
	TArray<FComputeKernelDefinitions> Defines;
};
