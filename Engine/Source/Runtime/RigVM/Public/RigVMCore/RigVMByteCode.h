// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMRegistry.h"
#include "RigVMByteCode.generated.h"

struct FRigVMByteCode;

// The code for a single operation within the RigVM
UENUM()
enum class ERigVMOpCode : uint8
{
	Execute_0_Args, // execute a rig function with 0 arguments
	Execute_1_Args, // execute a rig function with 1 arguments
	Execute_2_Args, // execute a rig function with 2 arguments
	Execute_3_Args, // execute a rig function with 3 arguments
	Execute_4_Args, // execute a rig function with 4 arguments
	Execute_5_Args, // execute a rig function with 5 arguments
	Execute_6_Args, // execute a rig function with 6 arguments
	Execute_7_Args, // execute a rig function with 7 arguments
	Execute_8_Args, // execute a rig function with 8 arguments
	Execute_9_Args, // execute a rig function with 9 arguments
	Execute_10_Args, // execute a rig function with 10 arguments
	Execute_11_Args, // execute a rig function with 11 arguments
	Execute_12_Args, // execute a rig function with 12 arguments
	Execute_13_Args, // execute a rig function with 13 arguments
	Execute_14_Args, // execute a rig function with 14 arguments
	Execute_15_Args, // execute a rig function with 15 arguments
	Execute_16_Args, // execute a rig function with 16 arguments
	Execute_17_Args, // execute a rig function with 17 arguments
	Execute_18_Args, // execute a rig function with 18 arguments
	Execute_19_Args, // execute a rig function with 19 arguments
	Execute_20_Args, // execute a rig function with 20 arguments
	Execute_21_Args, // execute a rig function with 21 arguments
	Execute_22_Args, // execute a rig function with 22 arguments
	Execute_23_Args, // execute a rig function with 23 arguments
	Execute_24_Args, // execute a rig function with 24 arguments
	Execute_25_Args, // execute a rig function with 25 arguments
	Execute_26_Args, // execute a rig function with 26 arguments
	Execute_27_Args, // execute a rig function with 27 arguments
	Execute_28_Args, // execute a rig function with 28 arguments
	Execute_29_Args, // execute a rig function with 29 arguments
	Execute_30_Args, // execute a rig function with 30 arguments
	Execute_31_Args, // execute a rig function with 31 arguments
	Execute_32_Args, // execute a rig function with 32 arguments
	Execute_33_Args, // execute a rig function with 33 arguments
	Execute_34_Args, // execute a rig function with 34 arguments
	Execute_35_Args, // execute a rig function with 35 arguments
	Execute_36_Args, // execute a rig function with 36 arguments
	Execute_37_Args, // execute a rig function with 37 arguments
	Execute_38_Args, // execute a rig function with 38 arguments
	Execute_39_Args, // execute a rig function with 39 arguments
	Execute_40_Args, // execute a rig function with 40 arguments
	Execute_41_Args, // execute a rig function with 41 arguments
	Execute_42_Args, // execute a rig function with 42 arguments
	Execute_43_Args, // execute a rig function with 43 arguments
	Execute_44_Args, // execute a rig function with 44 arguments
	Execute_45_Args, // execute a rig function with 45 arguments
	Execute_46_Args, // execute a rig function with 46 arguments
	Execute_47_Args, // execute a rig function with 47 arguments
	Execute_48_Args, // execute a rig function with 48 arguments
	Execute_49_Args, // execute a rig function with 49 arguments
	Execute_50_Args, // execute a rig function with 50 arguments
	Execute_51_Args, // execute a rig function with 51 arguments
	Execute_52_Args, // execute a rig function with 52 arguments
	Execute_53_Args, // execute a rig function with 53 arguments
	Execute_54_Args, // execute a rig function with 54 arguments
	Execute_55_Args, // execute a rig function with 55 arguments
	Execute_56_Args, // execute a rig function with 56 arguments
	Execute_57_Args, // execute a rig function with 57 arguments
	Execute_58_Args, // execute a rig function with 58 arguments
	Execute_59_Args, // execute a rig function with 59 arguments
	Execute_60_Args, // execute a rig function with 60 arguments
	Execute_61_Args, // execute a rig function with 61 arguments
	Execute_62_Args, // execute a rig function with 62 arguments
	Execute_63_Args, // execute a rig function with 63 arguments
	Execute_64_Args, // execute a rig function with 64 arguments
	Zero, // zero the memory of a given register
	BoolFalse, // set a given register to false
	BoolTrue, // set a given register to true
	Copy, // copy the content of one register to another
	Increment, // increment a int32 register
	Decrement, // decrement a int32 register
	Equals, // fill a bool register with the result of (A == B)
	NotEquals, // fill a bool register with the result of (A != B)
	JumpAbsolute, // jump to an absolute instruction index
	JumpForward, // jump forwards given a relative instruction index offset
	JumpBackward, // jump backwards given a relative instruction index offset
	JumpAbsoluteIf, // jump to an absolute instruction index based on a condition register
	JumpForwardIf, // jump forwards given a relative instruction index offset based on a condition register
	JumpBackwardIf, // jump backwards given a relative instruction index offset based on a condition register
	ChangeType, // change the type of a register
	Exit, // exit the execution loop
	Invalid
};

// Base class for all VM operations
struct RIGVM_API FRigVMBaseOp
{
	FRigVMBaseOp(ERigVMOpCode InOpCode = ERigVMOpCode::Invalid)
	: OpCode(InOpCode)
	{
	}

	ERigVMOpCode OpCode;
};


// execute a function
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

// zero the memory of a given register
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

// set a given register to false
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

// set a given register to true
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

// copy the content of one register to another
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

// increment a int32 register
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

// decrement a int32 register
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

// fill a bool register with the result of (A == B)
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

// fill a bool register with the result of (A != B)
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

// jump to a new instruction index.
// the instruction can be absolute, relative forward or relative backward
// based on the opcode 
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

// jump to a new instruction index based on a condition.
// the instruction can be absolute, relative forward or relative backward
// based on the opcode 
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

// change the type of a register
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

// exit the execution loop
struct RIGVM_API FRigVMExitOp : public FRigVMBaseOp
{
	FRigVMExitOp()
	: FRigVMBaseOp(ERigVMOpCode::Exit)
	{
	}
};

/**
 * The FRigVMInstruction represents
 * a single instruction within the VM.
 */
USTRUCT()
struct RIGVM_API FRigVMInstruction
{
	GENERATED_USTRUCT_BODY()

	FRigVMInstruction(ERigVMOpCode InOpCode = ERigVMOpCode::Invalid, uint64 InByteCodeIndex = UINT64_MAX)
		: OpCode(InOpCode)
		, ByteCodeIndex(InByteCodeIndex)
	{
	}

	UPROPERTY()
	ERigVMOpCode OpCode;

	UPROPERTY()
	uint64 ByteCodeIndex;
};

/**
 * The FRigVMInstructionArray represents all current instructions
 * within a RigVM and can be used to iterate over all operators and retrieve
 * each instruction's data.
 */
USTRUCT()
struct RIGVM_API FRigVMInstructionArray
{
	GENERATED_USTRUCT_BODY()

public:

	FRigVMInstructionArray();

	// Resets the data structure and removes all storage.
	void Reset();

	// Returns true if a given instruction index is valid.
	FORCEINLINE bool IsValidIndex(int32 InIndex) const { return Instructions.IsValidIndex(InIndex); }

	// Returns the number of instructions.
	FORCEINLINE int32 Num() const { return Instructions.Num(); }

	// const accessor for an instruction given its index
	FORCEINLINE const FRigVMInstruction& operator[](int32 InIndex) const { return Instructions[InIndex]; }

private:

	// hide utility constructor
	FRigVMInstructionArray(const FRigVMByteCode& InByteCode);

	UPROPERTY()
	TArray<FRigVMInstruction> Instructions;

	friend struct FRigVMByteCode;
};

/**
 * The FRigVMByteCode is a container to store a list of instructions with
 * their corresponding data. The byte code is then used within a VM to 
 * execute. To iterate over the instructions within the byte code you can 
 * use GetInstructions() to retrieve a FRigVMInstructionArray.
 */
USTRUCT()
struct RIGVM_API FRigVMByteCode
{
	GENERATED_USTRUCT_BODY()

public:

	FRigVMByteCode();

	// resets the container and removes all memory
	void Reset();

	// returns the number of instructions in this container
	uint64 Num() const;

	// adds an execute operator given its function index arguments
	uint64 AddExecuteOp(uint16 InFunctionIndex, const TArrayView<FRigVMArgument>& InArguments);

	// adds a zero operator to zero the memory of a given argument
	uint64 AddZeroOp(const FRigVMArgument& InArg);

	// adds a false operator to set a given argument to false
	uint64 AddFalseOp(const FRigVMArgument& InArg);

	// adds a true operator to set a given argument to true
	uint64 AddTrueOp(const FRigVMArgument& InArg);

	// adds a copy operator to copy the content of a source argument to a target argument
	uint64 AddCopyOp(const FRigVMArgument& InSource, const FRigVMArgument& InTarget, int32 InSourceOffset = INDEX_NONE, int32 InTargetOffset = INDEX_NONE, int32 InNumBytes = INDEX_NONE);

	// adds an increment operator to increment a int32 argument
	uint64 AddIncrementOp(const FRigVMArgument& InArg);

	// adds an decrement operator to decrement a int32 argument
	uint64 AddDecrementOp(const FRigVMArgument& InArg);

	// adds an equals operator to store the comparison result of A and B into a Result argument
	uint64 AddEqualsOp(const FRigVMArgument& InA, const FRigVMArgument& InB, const FRigVMArgument& InResult);

	// adds an not-equals operator to store the comparison result of A and B into a Result argument
	uint64 AddNotEqualsOp(const FRigVMArgument& InA, const FRigVMArgument& InB, const FRigVMArgument& InResult);

	// adds an absolute, forward or backward jump operator
	uint64 AddJumpOp(ERigVMOpCode InOpCode, uint16 InInstructionIndex);

	// adds an absolute, forward or backward jump operator based on a condition argument
	uint64 AddJumpIfOp(ERigVMOpCode InOpCode, uint16 InInstructionIndex, const FRigVMArgument& InConditionArg, bool bJumpWhenConditionIs = false);

	// adds a change-type operator to reuse a register for a smaller or same size type
	uint64 AddChangeTypeOp(FRigVMArgument InArg, ERigVMRegisterType InType, uint16 InElementSize, uint16 InElementCount, uint16 InSliceCount = 1);

	// adds an exit operator to exit the execution loop
	uint64 AddExitOp();

	// returns an instruction array for iterating over all operators
	FORCEINLINE FRigVMInstructionArray GetInstructions() const
	{
		return FRigVMInstructionArray(*this);
	}

	// returns the opcode at a given byte index
	FORCEINLINE ERigVMOpCode GetOpCodeAt(uint64 InByteCodeIndex) const
	{
		ensure(InByteCodeIndex >= 0 && InByteCodeIndex < ByteCode.Num());
		return (ERigVMOpCode)ByteCode[InByteCodeIndex];
	}

	// returns the size of the operator in bytes at a given byte index
	uint64 GetOpNumBytesAt(uint64 InByteCodeIndex, bool bIncludeArguments = true) const;

	// returns an operator at a given byte code index
	template<class OpType>
	FORCEINLINE const OpType& GetOpAt(uint64 InByteCodeIndex) const
	{
		ensure(InByteCodeIndex >= 0 && InByteCodeIndex <= ByteCode.Num() - sizeof(OpType));
		return *(const OpType*)(ByteCode.GetData() + InByteCodeIndex);
	}

	// returns an operator for a given instruction
	template<class OpType>
	FORCEINLINE const OpType& GetOpAt(const FRigVMInstruction& InInstruction) const
	{
		return GetOpAt<OpType>(InInstruction.ByteCodeIndex);
	}

	// returns a list of arguments at a given byte code index
	FORCEINLINE TArrayView<FRigVMArgument> GetArgumentsAt(uint64 InByteCodeIndex, uint16 InArgumentCount) const
	{
		ensure(InByteCodeIndex >= 0 && InByteCodeIndex <= ByteCode.Num() - sizeof(FRigVMArgument) * InArgumentCount);
		return TArrayView<FRigVMArgument>((FRigVMArgument*)(ByteCode.GetData() + InByteCodeIndex), InArgumentCount);
	}

	// returns the arguments for an execute operator / instruction at a given byte code index
	FORCEINLINE TArrayView<FRigVMArgument> GetArgumentsForExecuteOp(uint64 InByteCodeIndex) const
	{
		const FRigVMExecuteOp& ExecuteOp = GetOpAt<FRigVMExecuteOp>(InByteCodeIndex);
		return GetArgumentsAt(InByteCodeIndex + sizeof(FRigVMExecuteOp), ExecuteOp.GetArgumentCount());
	}

	// returns the arguments for a given execute instruction
	FORCEINLINE TArrayView<FRigVMArgument> GetArgumentsForExecuteOp(const FRigVMInstruction& InInstruction) const
	{
		const FRigVMExecuteOp& ExecuteOp = GetOpAt<FRigVMExecuteOp>(InInstruction);
		return GetArgumentsAt(InInstruction.ByteCodeIndex + sizeof(FRigVMExecuteOp), ExecuteOp.GetArgumentCount());
	}

	// returns the raw data of the byte code
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
