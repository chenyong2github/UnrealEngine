// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMRegistry.h"
#include "RigVMByteCode.generated.h"

struct FRigVMByteCode;

UENUM()
enum class ERigVMOpCode : uint8
{
	Execute_0_Args,
	Execute_1_Args,
	Execute_2_Args,
	Execute_3_Args,
	Execute_4_Args,
	Execute_5_Args,
	Execute_6_Args,
	Execute_7_Args,
	Execute_8_Args,
	Execute_9_Args,
	Execute_10_Args,
	Execute_11_Args,
	Execute_12_Args,
	Execute_13_Args,
	Execute_14_Args,
	Execute_15_Args,
	Execute_16_Args,
	Execute_17_Args,
	Execute_18_Args,
	Execute_19_Args,
	Execute_20_Args,
	Execute_21_Args,
	Execute_22_Args,
	Execute_23_Args,
	Execute_24_Args,
	Execute_25_Args,
	Execute_26_Args,
	Execute_27_Args,
	Execute_28_Args,
	Execute_29_Args,
	Execute_30_Args,
	Execute_31_Args,
	Execute_32_Args,
	Execute_33_Args,
	Execute_34_Args,
	Execute_35_Args,
	Execute_36_Args,
	Execute_37_Args,
	Execute_38_Args,
	Execute_39_Args,
	Execute_40_Args,
	Execute_41_Args,
	Execute_42_Args,
	Execute_43_Args,
	Execute_44_Args,
	Execute_45_Args,
	Execute_46_Args,
	Execute_47_Args,
	Execute_48_Args,
	Execute_49_Args,
	Execute_50_Args,
	Execute_51_Args,
	Execute_52_Args,
	Execute_53_Args,
	Execute_54_Args,
	Execute_55_Args,
	Execute_56_Args,
	Execute_57_Args,
	Execute_58_Args,
	Execute_59_Args,
	Execute_60_Args,
	Execute_61_Args,
	Execute_62_Args,
	Execute_63_Args,
	Execute_64_Args,
	Zero,
	BoolFalse,
	BoolTrue,
	Copy,
	Increment,
	Decrement,
	Equals,
	NotEquals,
	JumpAbsolute,
	JumpForward,
	JumpBackward,
	JumpAbsoluteIf,
	JumpForwardIf,
	JumpBackwardIf,
	ChangeType,
	Exit,
	Invalid
};

struct RIGVM_API FRigVMBaseOp
{
	FRigVMBaseOp(ERigVMOpCode InOpCode = ERigVMOpCode::Invalid)
	: OpCode(InOpCode)
	{
	}

	ERigVMOpCode OpCode;
};

struct RIGVM_API FRigVMExecuteOp : public FRigVMBaseOp
{
	FRigVMExecuteOp()
	: FRigVMBaseOp()
	, FunctionIndex(INDEX_NONE)
	{
	}

	FRigVMExecuteOp(uint16 InFunctionIndex, uint8 InArgumentCount)
	: FRigVMBaseOp((ERigVMOpCode)(uint8(ERigVMOpCode::Execute_0_Args) + InArgumentCount))
	, FunctionIndex(InFunctionIndex)
	{
	}

	uint16 FunctionIndex;

	FORCEINLINE uint8 GetArgumentCount() const { return uint8(OpCode) - uint8(ERigVMOpCode::Execute_0_Args); }
};

struct RIGVM_API FRigVMZeroOp : public FRigVMBaseOp
{
	FRigVMZeroOp()
		: FRigVMBaseOp(ERigVMOpCode::Zero)
		, Arg()
	{
	}

	FRigVMZeroOp(FRigVMArgument InArg)
		: FRigVMBaseOp(ERigVMOpCode::Zero)
		, Arg(InArg)
	{
	}

	FRigVMArgument Arg;
};

struct RIGVM_API FRigVMFalseOp : public FRigVMBaseOp
{
	FRigVMFalseOp()
		: FRigVMBaseOp(ERigVMOpCode::BoolFalse)
		, Arg()
	{
	}

	FRigVMFalseOp(FRigVMArgument InArg)
		: FRigVMBaseOp(ERigVMOpCode::BoolFalse)
		, Arg(InArg)
	{
	}

	FRigVMArgument Arg;
};

struct RIGVM_API FRigVMTrueOp : public FRigVMBaseOp
{
	FRigVMTrueOp()
		: FRigVMBaseOp(ERigVMOpCode::BoolTrue)
		, Arg()
	{
	}

	FRigVMTrueOp(FRigVMArgument InArg)
		: FRigVMBaseOp(ERigVMOpCode::BoolTrue)
		, Arg(InArg)
	{
	}

	FRigVMArgument Arg;
};

struct RIGVM_API FRigVMCopyOp : public FRigVMBaseOp
{
	FRigVMCopyOp()
	: FRigVMBaseOp(ERigVMOpCode::Copy)
	, Source()
	, Target()
	, SourceOffset(INDEX_NONE)
	, TargetOffset(INDEX_NONE)
	, NumBytes(INDEX_NONE)
	{
	}

	FRigVMCopyOp(
		FRigVMArgument InSource,
		FRigVMArgument InTarget,
		int32 InSourceOffset = INDEX_NONE,
		int32 InTargetOffset = INDEX_NONE,
		int32 InNumBytes = INDEX_NONE
	)
		: FRigVMBaseOp(ERigVMOpCode::Copy)
		, Source(InSource)
		, Target(InTarget)
		, SourceOffset(InSourceOffset)
		, TargetOffset(InTargetOffset)
		, NumBytes(InNumBytes)
	{
	}

	FRigVMArgument Source;
	FRigVMArgument Target;
	int32 SourceOffset;
	int32 TargetOffset;
	int32 NumBytes;
};

struct RIGVM_API FRigVMIncrementOp : public FRigVMBaseOp
{
	FRigVMIncrementOp()
	: FRigVMBaseOp(ERigVMOpCode::Increment)
	, Arg()
	{
	}

	FRigVMIncrementOp(FRigVMArgument InArg)
	: FRigVMBaseOp(ERigVMOpCode::Increment)
	, Arg(InArg)
	{
	}

	FRigVMArgument Arg;
};

struct RIGVM_API FRigVMDecrementOp : public FRigVMBaseOp
{
	FRigVMDecrementOp()
	: FRigVMBaseOp(ERigVMOpCode::Decrement)
	, Arg()
	{
	}

	FRigVMDecrementOp(FRigVMArgument InArg)
	: FRigVMBaseOp(ERigVMOpCode::Decrement)
	, Arg(InArg)
	{
	}

	FRigVMArgument Arg;
};

struct RIGVM_API FRigVMEqualsOp : public FRigVMBaseOp
{
	FRigVMEqualsOp()
	: FRigVMBaseOp(ERigVMOpCode::Equals)
	, A()
	, B()
	, Result()
	{
	}

	FRigVMEqualsOp(
		FRigVMArgument InA,
		FRigVMArgument InB,
		FRigVMArgument InResult
	)
	: FRigVMBaseOp(ERigVMOpCode::Equals)
	, A(InA)
	, B(InB)
	, Result(InResult)
	{
	}

	FRigVMArgument A;
	FRigVMArgument B;
	FRigVMArgument Result;
};

struct RIGVM_API FRigVMNotEqualsOp : public FRigVMBaseOp
{
	FRigVMNotEqualsOp()
	: FRigVMBaseOp(ERigVMOpCode::NotEquals)
	, A()
	, B()
	, Result()
	{
	}

	FRigVMNotEqualsOp(
		FRigVMArgument InA,
		FRigVMArgument InB,
		FRigVMArgument InResult
	)
	: FRigVMBaseOp(ERigVMOpCode::NotEquals)
	, A(InA)
	, B(InB)
	, Result(InResult)
	{
	}

	FRigVMArgument A;
	FRigVMArgument B;
	FRigVMArgument Result;
};

struct RIGVM_API FRigVMJumpOp : public FRigVMBaseOp
{
	FRigVMJumpOp()
	: FRigVMBaseOp(ERigVMOpCode::Invalid)
	, InstructionIndex(INDEX_NONE)
	{
	}

	FRigVMJumpOp(ERigVMOpCode InOpCode, int32 InInstructionIndex)
	: FRigVMBaseOp(InOpCode)
	, InstructionIndex(InInstructionIndex)
	{
		ensure(uint8(InOpCode) >= uint8(ERigVMOpCode::JumpAbsolute));
		ensure(uint8(InOpCode) <= uint8(ERigVMOpCode::JumpBackward));
	}

	int32 InstructionIndex;
};

struct RIGVM_API FRigVMJumpIfOp : public FRigVMBaseOp
{
	FRigVMJumpIfOp()
		: FRigVMBaseOp(ERigVMOpCode::Invalid)
		, InstructionIndex(INDEX_NONE)
		, ConditionArg()
		, Condition(true)
	{
	}

	FRigVMJumpIfOp(ERigVMOpCode InOpCode, int32 InInstructionIndex, FRigVMArgument InConditionArg, bool InCondition = false)
		: FRigVMBaseOp(InOpCode)
		, InstructionIndex(InInstructionIndex)
		, ConditionArg(InConditionArg)
		, Condition(InCondition)
	{
		ensure(uint8(InOpCode) >= uint8(ERigVMOpCode::JumpAbsoluteIf));
		ensure(uint8(InOpCode) <= uint8(ERigVMOpCode::JumpBackwardIf));
	}

	int32 InstructionIndex;
	FRigVMArgument ConditionArg;
	bool Condition;
};

struct RIGVM_API FRigVMChangeTypeOp : public FRigVMBaseOp
{
	FRigVMChangeTypeOp()
		: FRigVMBaseOp(ERigVMOpCode::Invalid)
		, Arg()
		, Type(ERigVMRegisterType::Invalid)
		, ElementSize(0)
		, ElementCount(0)
		, SliceCount(0)
	{
	}

	FRigVMChangeTypeOp(FRigVMArgument InArg, ERigVMRegisterType InType, uint16 InElementSize, uint16 InElementCount, uint16 InSliceCount)
		: FRigVMBaseOp(ERigVMOpCode::ChangeType)
		, Arg(InArg)
		, Type(InType)
		, ElementSize(InElementSize)
		, ElementCount(InElementCount)
		, SliceCount(InSliceCount)
	{
	}

	FRigVMArgument Arg;
	ERigVMRegisterType Type;
	uint16 ElementSize;
	uint16 ElementCount;
	uint16 SliceCount;
};

struct RIGVM_API FRigVMExitOp : public FRigVMBaseOp
{
	FRigVMExitOp()
	: FRigVMBaseOp(ERigVMOpCode::Exit)
	{
	}
};

USTRUCT()
struct RIGVM_API FRigVMByteCodeTableEntry
{
	GENERATED_USTRUCT_BODY()

	FRigVMByteCodeTableEntry(ERigVMOpCode InOpCode = ERigVMOpCode::Invalid, uint64 InByteCodeIndex = UINT64_MAX)
		: OpCode(InOpCode)
		, ByteCodeIndex(InByteCodeIndex)
	{
	}

	UPROPERTY()
	ERigVMOpCode OpCode;

	UPROPERTY()
	uint64 ByteCodeIndex;
};

USTRUCT()
struct RIGVM_API FRigVMByteCodeTable
{
	GENERATED_USTRUCT_BODY()

public:

	FRigVMByteCodeTable();

	void Reset();
	FORCEINLINE bool IsValidIndex(int32 InIndex) const { return Entries.IsValidIndex(InIndex); }
	FORCEINLINE int32 Num() const { return Entries.Num(); }
	FORCEINLINE const FRigVMByteCodeTableEntry& operator[](int32 InIndex) const { return Entries[InIndex]; }

private:

	// hide utility constructor
	FRigVMByteCodeTable(const FRigVMByteCode& InByteCode);

	UPROPERTY()
	TArray<FRigVMByteCodeTableEntry> Entries;

	friend struct FRigVMByteCode;
};

USTRUCT()
struct RIGVM_API FRigVMByteCode
{
	GENERATED_USTRUCT_BODY()

public:

	FRigVMByteCode();

	void Reset();
	uint64 Num() const;

	uint64 AddExecuteOp(uint16 InFunctionIndex, const TArrayView<FRigVMArgument>& InArguments);
	uint64 AddZeroOp(const FRigVMArgument& InArg);
	uint64 AddFalseOp(const FRigVMArgument& InArg);
	uint64 AddTrueOp(const FRigVMArgument& InArg);
	uint64 AddCopyOp(const FRigVMArgument& InSource, const FRigVMArgument& InTarget, int32 InSourceOffset = INDEX_NONE, int32 InTargetOffset = INDEX_NONE, int32 InNumBytes = INDEX_NONE);
	uint64 AddIncrementOp(const FRigVMArgument& InArg);
	uint64 AddDecrementOp(const FRigVMArgument& InArg);
	uint64 AddEqualsOp(const FRigVMArgument& InA, const FRigVMArgument& InB, const FRigVMArgument& InResult);
	uint64 AddNotEqualsOp(const FRigVMArgument& InA, const FRigVMArgument& InB, const FRigVMArgument& InResult);
	uint64 AddJumpOp(ERigVMOpCode InOpCode, uint16 InInstructionIndex);
	uint64 AddJumpIfOp(ERigVMOpCode InOpCode, uint16 InInstructionIndex, const FRigVMArgument& InConditionArg, bool bInCondition = false);
	uint64 AddChangeTypeOp(FRigVMArgument InArg, ERigVMRegisterType InType, uint16 InElementSize, uint16 InElementCount, uint16 InSliceCount = 1);
	uint64 AddExitOp();

	FORCEINLINE FRigVMByteCodeTable GetTable() const
	{
		return FRigVMByteCodeTable(*this);
	}

	FORCEINLINE ERigVMOpCode GetOpCodeAt(uint64 InByteCodeIndex) const
	{
		ensure(InByteCodeIndex >= 0 && InByteCodeIndex < ByteCode.Num());
		return (ERigVMOpCode)ByteCode[InByteCodeIndex];
	}

	uint64 GetOpNumBytesAt(uint64 InByteCodeIndex, bool bIncludeArguments = true) const;

	template<class OpType>
	FORCEINLINE const OpType& GetOpAt(uint64 InByteCodeIndex) const
	{
		ensure(InByteCodeIndex >= 0 && InByteCodeIndex <= ByteCode.Num() - sizeof(OpType));
		return *(const OpType*)(ByteCode.GetData() + InByteCodeIndex);
	}

	template<class OpType>
	FORCEINLINE const OpType& GetOpAt(const FRigVMByteCodeTableEntry& InEntry) const
	{
		return GetOpAt<OpType>(InEntry.ByteCodeIndex);
	}

	FORCEINLINE TArrayView<FRigVMArgument> GetArgumentsAt(uint64 InByteCodeIndex, uint16 InArgumentCount) const
	{
		ensure(InByteCodeIndex >= 0 && InByteCodeIndex <= ByteCode.Num() - sizeof(FRigVMArgument) * InArgumentCount);
		return TArrayView<FRigVMArgument>((FRigVMArgument*)(ByteCode.GetData() + InByteCodeIndex), InArgumentCount);
	}

	FORCEINLINE TArrayView<FRigVMArgument> GetArgumentsForExecuteOp(uint64 InByteCodeIndex) const
	{
		const FRigVMExecuteOp& ExecuteOp = GetOpAt<FRigVMExecuteOp>(InByteCodeIndex);
		return GetArgumentsAt(InByteCodeIndex + sizeof(FRigVMExecuteOp), ExecuteOp.GetArgumentCount());
	}

	FORCEINLINE TArrayView<FRigVMArgument> GetArgumentsForExecuteOp(const FRigVMByteCodeTableEntry& InEntry) const
	{
		const FRigVMExecuteOp& ExecuteOp = GetOpAt<FRigVMExecuteOp>(InEntry);
		return GetArgumentsAt(InEntry.ByteCodeIndex + sizeof(FRigVMExecuteOp), ExecuteOp.GetArgumentCount());
	}

	FORCEINLINE const TArrayView<uint8> GetByteCode() const
	{
		const uint8* Data = ByteCode.GetData();
		return TArrayView<uint8>((uint8*)Data, ByteCode.Num());
	}

private:

	template<class OpType>
	FORCEINLINE uint64 AddOp(const OpType& InOp)
	{
		uint64 ByteIndex = (uint64)ByteCode.AddUninitialized(sizeof(OpType));
		FMemory::Memcpy(ByteCode.GetData() + ByteIndex, &InOp, sizeof(OpType));
		return ByteIndex;
	}

	// memory for all functions
	UPROPERTY()
	TArray<uint8> ByteCode;
};
