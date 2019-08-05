// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVMByteCode.h"

FRigVMByteCodeTable::FRigVMByteCodeTable()
{
}

FRigVMByteCodeTable::FRigVMByteCodeTable(const FRigVMByteCode& InByteCode)
{
	uint64 ByteIndex = 0;
	while (ByteIndex < InByteCode.Num())
	{
		ERigVMOpCode OpCode = InByteCode.GetOpCodeAt(ByteIndex);
		Entries.Add(FRigVMByteCodeTableEntry(OpCode, ByteIndex));
		ByteIndex += InByteCode.GetOpNumBytesAt(ByteIndex);
	}
}

void FRigVMByteCodeTable::Reset()
{
	Entries.Reset();
}

FRigVMByteCode::FRigVMByteCode()
{
}

void FRigVMByteCode::Reset()
{
	ByteCode.Reset();
}

uint64 FRigVMByteCode::Num() const
{
	return (uint64)ByteCode.Num();
}

uint64 FRigVMByteCode::GetOpNumBytesAt(uint64 InByteCodeIndex, bool bIncludeArguments) const
{
	ERigVMOpCode OpCode = GetOpCodeAt(InByteCodeIndex);
	switch (OpCode)
	{
		case ERigVMOpCode::Copy:
		{
			return (uint64)sizeof(FRigVMCopyOp);
		}
		case ERigVMOpCode::Increment:
		{
			return (uint64)sizeof(FRigVMIncrementOp);
		}
		case ERigVMOpCode::Decrement:
		{
			return (uint64)sizeof(FRigVMDecrementOp);
		}
		case ERigVMOpCode::Equals:
		{
			return (uint64)sizeof(FRigVMEqualsOp);
		}
		case ERigVMOpCode::NotEquals:
		{
			return (uint64)sizeof(FRigVMNotEqualsOp);
		}
		case ERigVMOpCode::Jump:
		{
			return (uint64)sizeof(FRigVMJumpOp);
		}
		case ERigVMOpCode::JumpIfTrue:
		{
			return (uint64)sizeof(FRigVMJumpIfTrueOp);
		}
		case ERigVMOpCode::JumpIfFalse:
		{
			return (uint64)sizeof(FRigVMJumpIfFalseOp);
		}
		case ERigVMOpCode::Execute:
		{
			uint64 NumBytes = (uint64)sizeof(FRigVMExecuteOp);
			if(bIncludeArguments)
			{
				const FRigVMExecuteOp& ExecuteOp = GetOpAt<FRigVMExecuteOp>(InByteCodeIndex);
				NumBytes += (uint64)ExecuteOp.ArgumentCount * (uint64)sizeof(FRigVMArgument);
			}
			return NumBytes;
		}
		case ERigVMOpCode::Exit:
		{
			return (uint64)sizeof(FRigVMExitOp);
		}
		case ERigVMOpCode::Invalid:
		{
			ensure(false);
			return 0;
		}
	}
	return 0;
}

uint64 FRigVMByteCode::AddCopyOp(const FRigVMArgument& InSource, const FRigVMArgument& InTarget, int32 InSourceOffset, int32 InTargetOffset, int32 InNumBytes)
{
	FRigVMCopyOp Op(InSource, InTarget, InSourceOffset, InTargetOffset, InNumBytes);
	return AddOp(Op);
}

uint64 FRigVMByteCode::AddIncrementOp(const FRigVMArgument& InArg)
{
	FRigVMIncrementOp Op(InArg);
	return AddOp(Op);
}

uint64 FRigVMByteCode::AddDecrementOp(const FRigVMArgument& InArg)
{
	FRigVMDecrementOp Op(InArg);
	return AddOp(Op);
}

uint64 FRigVMByteCode::AddEqualsOp(const FRigVMArgument& InA, const FRigVMArgument& InB, const FRigVMArgument& InResult)
{
	FRigVMEqualsOp Op(InA, InB, InResult);
	return AddOp(Op);
}

uint64 FRigVMByteCode::AddNotEqualsOp(const FRigVMArgument& InA, const FRigVMArgument& InB, const FRigVMArgument& InResult)
{
	FRigVMNotEqualsOp Op(InA, InB, InResult);
	return AddOp(Op);
}

uint64 FRigVMByteCode::AddJumpOp(uint64 InByteCodeIndex)
{
	FRigVMJumpOp Op(InByteCodeIndex);
	return AddOp(Op);
}

uint64 FRigVMByteCode::AddJumpIfTrueOp(uint64 InByteCodeIndex, const FRigVMArgument& InCondition)
{
	FRigVMJumpIfTrueOp Op(InByteCodeIndex, InCondition);
	return AddOp(Op);
}

uint64 FRigVMByteCode::AddJumpIfFalseOp(uint64 InByteCodeIndex, const FRigVMArgument& InCondition)
{
	FRigVMJumpIfFalseOp Op(InByteCodeIndex, InCondition);
	return AddOp(Op);
}

uint64 FRigVMByteCode::AddExecuteOp(uint16 InFunctionIndex, const TArrayView<FRigVMArgument>& InArguments)
{
	FRigVMExecuteOp Op(InFunctionIndex, (uint16)InArguments.Num());
	uint64 OpByteIndex = AddOp(Op);
	uint64 ArgumentsByteIndex = (uint64)ByteCode.AddUninitialized(sizeof(FRigVMArgument) * InArguments.Num());
	FMemory::Memcpy(ByteCode.GetData() + ArgumentsByteIndex, InArguments.GetData(), sizeof(FRigVMArgument) * InArguments.Num());
	return OpByteIndex;
}

uint64 FRigVMByteCode::AddExitOp()
{
	FRigVMExitOp Op;
	return AddOp(Op);
}

