// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMRegistry.h"
#include "RigVMByteCode.generated.h"

struct FRigVMByteCode;

UENUM()
enum class ERigVMOpCode : uint8
{
	Copy,
	Increment,
	Decrement,
	Equals,
	NotEquals,
	Jump,
	JumpIfTrue,
	JumpIfFalse,
	Execute,
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
	: FRigVMBaseOp(ERigVMOpCode::Jump)
	, InstructionIndex(INDEX_NONE)
	{
	}

	FRigVMJumpOp(int32 InInstructionIndex)
	: FRigVMBaseOp(ERigVMOpCode::Jump)
	, InstructionIndex(InInstructionIndex)
	{
	}

	int32 InstructionIndex;
};

struct RIGVM_API FRigVMJumpIfTrueOp : public FRigVMBaseOp
{
	FRigVMJumpIfTrueOp()
	: FRigVMBaseOp(ERigVMOpCode::JumpIfTrue)
	, InstructionIndex(INDEX_NONE)
	, Condition()
	{
	}

	FRigVMJumpIfTrueOp(int32 InInstructionIndex, FRigVMArgument InCondition)
	: FRigVMBaseOp(ERigVMOpCode::JumpIfTrue)
	, InstructionIndex(InInstructionIndex)
	, Condition(InCondition)
	{
	}

	int32 InstructionIndex;
	FRigVMArgument Condition;
};

struct RIGVM_API FRigVMJumpIfFalseOp : public FRigVMBaseOp
{
	FRigVMJumpIfFalseOp()
	: FRigVMBaseOp(ERigVMOpCode::JumpIfFalse)
	, InstructionIndex(INDEX_NONE)
	, Condition()
	{
	}

	FRigVMJumpIfFalseOp(int32 InInstructionIndex, FRigVMArgument InCondition)
	: FRigVMBaseOp(ERigVMOpCode::JumpIfFalse)
	, InstructionIndex(InInstructionIndex)
	, Condition(InCondition)
	{
	}

	int32 InstructionIndex;
	FRigVMArgument Condition;
};

struct RIGVM_API FRigVMExecuteOp : public FRigVMBaseOp
{
	FRigVMExecuteOp()
	: FRigVMBaseOp(ERigVMOpCode::Execute)
	, FunctionIndex(INDEX_NONE)
	, ArgumentCount(0)
	{
	}

	FRigVMExecuteOp(uint16 InFunctionIndex, uint16 InArgumentCount)
	: FRigVMBaseOp(ERigVMOpCode::Execute)
	, FunctionIndex(InFunctionIndex)
	, ArgumentCount(InArgumentCount)
	{
	}

	uint16 FunctionIndex;
	uint16 ArgumentCount;
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

	uint64 AddCopyOp(const FRigVMArgument& InSource, const FRigVMArgument& InTarget, int32 InSourceOffset = INDEX_NONE, int32 InTargetOffset = INDEX_NONE, int32 InNumBytes = INDEX_NONE);
	uint64 AddIncrementOp(const FRigVMArgument& InArg);
	uint64 AddDecrementOp(const FRigVMArgument& InArg);
	uint64 AddEqualsOp(const FRigVMArgument& InA, const FRigVMArgument& InB, const FRigVMArgument& InResult);
	uint64 AddNotEqualsOp(const FRigVMArgument& InA, const FRigVMArgument& InB, const FRigVMArgument& InResult);
	uint64 AddJumpOp(uint64 InByteCodeIndex);
	uint64 AddJumpIfTrueOp(uint64 InByteCodeIndex, const FRigVMArgument& InCondition);
	uint64 AddJumpIfFalseOp(uint64 InByteCodeIndex, const FRigVMArgument& InCondition);
	uint64 AddExecuteOp(uint16 InFunctionIndex, const TArrayView<FRigVMArgument>& InArguments);
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
		ensure(OpType().OpCode == InEntry.OpCode);
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
		return GetArgumentsAt(InByteCodeIndex + sizeof(FRigVMExecuteOp), ExecuteOp.ArgumentCount);
	}

	FORCEINLINE TArrayView<FRigVMArgument> GetArgumentsForExecuteOp(const FRigVMByteCodeTableEntry& InEntry) const
	{
		const FRigVMExecuteOp& ExecuteOp = GetOpAt<FRigVMExecuteOp>(InEntry);
		return GetArgumentsAt(InEntry.ByteCodeIndex + sizeof(FRigVMExecuteOp), ExecuteOp.ArgumentCount);
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

	// storage for all functions
	UPROPERTY()
	TArray<uint8> ByteCode;
};
