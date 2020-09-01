// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMArray.h"
#include "RigVMExternalVariable.h"
#include "RigVMExecuteContext.generated.h"

USTRUCT()
struct RIGVM_API FRigVMSlice
{
public:

	GENERATED_BODY()

	FORCEINLINE FRigVMSlice()
	: LowerBound(0)
	, UpperBound(0)
	, Index(INDEX_NONE)
	{
		Reset();
	}

	FORCEINLINE FRigVMSlice(int32 InCount)
		: LowerBound(0)
		, UpperBound(InCount - 1)
		, Index(INDEX_NONE)
	{
		Reset();
	}

	FORCEINLINE FRigVMSlice(int32 InCount, const FRigVMSlice& InParent)
		: LowerBound(InParent.GetIndex() * InCount)
		, UpperBound((InParent.GetIndex() + 1) * InCount - 1)
		, Index(INDEX_NONE)
	{
		Reset();
	}

	FORCEINLINE bool IsValid() const
	{ 
		return Index != INDEX_NONE;
	}
	
	FORCEINLINE bool IsComplete() const
	{
		return Index > UpperBound;
	}

	FORCEINLINE int32 GetIndex() const
	{
		return Index;
	}

	FORCEINLINE void SetIndex(int32 InIndex)
	{
		Index = InIndex;
	}

	FORCEINLINE int32 GetRelativeIndex() const
	{
		return Index - LowerBound;
	}

	FORCEINLINE void SetRelativeIndex(int32 InIndex)
	{
		Index = InIndex + LowerBound;
	}

	FORCEINLINE float GetRelativeRatio() const
	{
		return float(GetRelativeIndex()) / float(FMath::Max<int32>(1, Num() - 1));
	}

	FORCEINLINE int32 Num() const
	{
		return 1 + UpperBound - LowerBound;
	}

	FORCEINLINE int32 TotalNum() const
	{
		return UpperBound + 1;
	}

	FORCEINLINE operator bool() const
	{
		return IsValid();
	}

	FORCEINLINE bool operator !() const
	{
		return !IsValid();
	}

	FORCEINLINE operator int32() const
	{
		return Index;
	}

	FORCEINLINE FRigVMSlice& operator++()
	{
		Index++;
		return *this;
	}

	FORCEINLINE FRigVMSlice operator++(int32)
	{
		FRigVMSlice TemporaryCopy = *this;
		++*this;
		return TemporaryCopy;
	}

	FORCEINLINE bool Next()
	{
		if (!IsValid())
		{
			return false;
		}

		if (IsComplete())
		{
			return false;
		}

		Index++;
		return true;
	}

	FORCEINLINE void Reset()
	{
		if (UpperBound >= LowerBound)
		{
			Index = LowerBound;
		}
		else
		{
			Index = INDEX_NONE;
		}
	}

private:

	int32 LowerBound;
	int32 UpperBound;
	int32 Index;
};

/**
 * The execute context is used for mutable nodes to
 * indicate execution order.
 */
USTRUCT()
struct RIGVM_API FRigVMExecuteContext
{
	GENERATED_BODY()

	FORCEINLINE FRigVMExecuteContext()
		: EventName(NAME_None)
		, FunctionName(NAME_None)
		, InstructionIndex(0)
	{
		Reset();
	}

	FORCEINLINE void Reset()
	{
		Slices.Reset();
		Slices.Add(FRigVMSlice());
		SliceOffsets.Reset();
		InstructionIndex = 0;
		ExternalVariables.Reset();
	}

	FORCEINLINE const FRigVMSlice& GetSlice() const
	{
		const int32 SliceOffset = (int32)SliceOffsets[InstructionIndex];
		if (SliceOffset == 0)
		{
			return Slices.Last();
		}
		const int32 UpperBound = Slices.Num() - 1;
		return Slices[FMath::Clamp<int32>(UpperBound - SliceOffset, 0, UpperBound)];
	}

	FORCEINLINE void BeginSlice(int32 InCount, int32 InRelativeIndex = 0)
	{
		ensure(!IsSliceComplete());
		Slices.Add(FRigVMSlice(InCount, Slices.Last()));
		Slices.Last().SetRelativeIndex(InRelativeIndex);
	}

	FORCEINLINE void EndSlice()
	{
		ensure(Slices.Num() > 1);
		Slices.Pop();
	}

	FORCEINLINE void IncrementSlice()
	{
		FRigVMSlice& ActiveSlice = Slices.Last();
		ActiveSlice++;
	}

	FORCEINLINE bool IsSliceComplete() const
	{
		return GetSlice().IsComplete();
	}

	FORCEINLINE const FRigVMExternalVariable* FindExternalVariable(const FName& InExternalVariableName) const
	{
		for (const FRigVMExternalVariable& ExternalVariable : ExternalVariables)
		{
			if (ExternalVariable.Name == InExternalVariableName)
			{
				return &ExternalVariable;
			}
		}
		return nullptr;
	}

	FORCEINLINE FRigVMExternalVariable* FindExternalVariable(const FName& InExternalVariableName)
	{
		for (FRigVMExternalVariable& ExternalVariable : ExternalVariables)
		{
			if (ExternalVariable.Name == InExternalVariableName)
			{
				return &ExternalVariable;
			}
		}
		return nullptr;
	}

	FName EventName;
	FName FunctionName;
	uint16 InstructionIndex;
	FRigVMFixedArray<void*> OpaqueArguments;
	TArray<FRigVMExternalVariable> ExternalVariables;
	TArray<FRigVMSlice> Slices;
	TArray<uint16> SliceOffsets;
};
