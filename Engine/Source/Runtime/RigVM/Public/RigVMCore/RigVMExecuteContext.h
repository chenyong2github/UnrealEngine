// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMDefines.h"
#include "RigVMArray.h"
#include "RigVMExternalVariable.h"
#include "RigVMModule.h"
#include "Logging/TokenizedMessage.h"
#include "RigVMExecuteContext.generated.h"

struct FRigVMExecuteContext;
class URigVM;

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

USTRUCT()
struct RIGVM_API FRigVMRuntimeSettings
{
	GENERATED_BODY()

	/**
	 * The largest allowed size for arrays within the Control Rig VM.
	 * Accessing or creating larger arrays will cause runtime errors in the rig.
	 */
	UPROPERTY(EditAnywhere, Category = "VM")
	int32 MaximumArraySize = 2048;

#if WITH_EDITORONLY_DATA
	// When enabled records the timing of each instruction / node
	// on each node and within the execution stack window.
	// Keep in mind when looking at nodes in a function the duration
	// represents the accumulated duration of all invocations
	// of the function currently running.
	UPROPERTY(EditAnywhere, Category = "VM")
	bool bEnableProfiling = false;
#endif

	/*
	 * The function to use for logging anything from the VM to the host
	 */
	TFunction<void(EMessageSeverity::Type,const FRigVMExecuteContext*,const FString&)> LogFunction = nullptr;
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
		, VM(nullptr)
		, RuntimeSettings()
		, LastExecutionMicroSeconds() 
	{
		Reset();
	}

	FORCEINLINE void Reset()
	{
		Slices.Reset();
		Slices.Add(FRigVMSlice());
		SliceOffsets.Reset();
		InstructionIndex = 0;
		VM = nullptr;
		ExternalVariables.Reset();
	}

	FORCEINLINE void CopyFrom(const FRigVMExecuteContext& Other)
	{
		EventName = Other.EventName;
		FunctionName = Other.FunctionName;
		InstructionIndex = Other.InstructionIndex;
		VM = Other.VM;
		RuntimeSettings = Other.RuntimeSettings;
		ExternalVariables = Other.ExternalVariables;
		OpaqueArguments = Other.OpaqueArguments;
		Slices = Other.Slices;
		SliceOffsets = Other.SliceOffsets;
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

	FORCEINLINE void Log(EMessageSeverity::Type InSeverity, const FString& InMessage) const
	{
		if(RuntimeSettings.LogFunction)
		{
			RuntimeSettings.LogFunction(InSeverity, this, InMessage);
		}
		else
		{
			if(InSeverity == EMessageSeverity::Error)
			{
				UE_LOG(LogRigVM, Error, TEXT("Instruction %d: %s"), InstructionIndex, *InMessage);
			}
			else if(InSeverity == EMessageSeverity::Warning)
			{
				UE_LOG(LogRigVM, Warning, TEXT("Instruction %d: %s"), InstructionIndex, *InMessage);
			}
			else
			{
				UE_LOG(LogRigVM, Display, TEXT("Instruction %d: %s"), InstructionIndex, *InMessage);
			}
		}
	}

	template <typename FmtType, typename... Types>
	FORCEINLINE void Logf(EMessageSeverity::Type InSeverity, const FmtType& Fmt, Types... Args) const
	{
		Log(InSeverity, FString::Printf(Fmt, Args...));
	}

	FORCEINLINE bool IsValidArrayIndex(int32 InIndex, int32 InArraySize)
	{
		if(InIndex < 0 || InIndex >= InArraySize)
		{
			static const TCHAR OutOfBoundsFormat[] = TEXT("Array Index (%d) out of bounds (count %d).");
			Logf(EMessageSeverity::Error, OutOfBoundsFormat, InIndex, InArraySize);
			return false;
		}
		return true;
	}

	FORCEINLINE bool IsValidArraySize(int32 InSize) const
	{
		if(InSize < 0 || InSize > RuntimeSettings.MaximumArraySize)
		{
			static const TCHAR OutOfBoundsFormat[] = TEXT("Array Size (%d) larger than allowed maximum (%d).\nCheck VMRuntimeSettings in class settings.");
			Logf(EMessageSeverity::Error, OutOfBoundsFormat, InSize, RuntimeSettings.MaximumArraySize);
			return false;
		}
		return true;
	}

	FORCEINLINE void SetRuntimeSettings(FRigVMRuntimeSettings InRuntimeSettings)
	{
		RuntimeSettings = InRuntimeSettings;
		check(RuntimeSettings.MaximumArraySize > 0);
	}

	FName EventName;
	FName FunctionName;
	uint16 InstructionIndex;
	URigVM* VM;
	FRigVMRuntimeSettings RuntimeSettings;
#if UE_RIGVM_UCLASS_BASED_STORAGE_DISABLED
	FRigVMFixedArray<void*> OpaqueArguments;
#else
	TArrayView<void*> OpaqueArguments;
#endif
	TArray<FRigVMExternalVariable> ExternalVariables;
	TArray<FRigVMSlice> Slices;
	TArray<uint16> SliceOffsets;
	double LastExecutionMicroSeconds;
};
