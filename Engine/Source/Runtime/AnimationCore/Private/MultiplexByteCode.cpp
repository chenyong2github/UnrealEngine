// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MultiplexByteCode.h"

FMultiplexByteCodeTable::FMultiplexByteCodeTable()
{
}

FMultiplexByteCodeTable::FMultiplexByteCodeTable(const FMultiplexByteCode& InByteCode)
{
	uint64 Address = 0;
	while (Address < InByteCode.Num())
	{
		EMultiplexOpCode OpCode = InByteCode.GetOpCodeAt(Address);
		Entries.Add(FMultiplexByteCodeTableEntry(OpCode, Address));
		Address += InByteCode.GetOpNumBytesAt(Address);
	}
}

FMultiplexByteCode::FMultiplexByteCode()
{
}

void FMultiplexByteCode::Reset()
{
	ByteCode.Reset();
}

uint64 FMultiplexByteCode::Num() const
{
	return (uint64)ByteCode.Num();
}

uint64 FMultiplexByteCode::GetOpNumBytesAt(uint64 InByteCodeIndex, bool bIncludeArguments) const
{
	EMultiplexOpCode OpCode = GetOpCodeAt(InByteCodeIndex);
	switch (OpCode)
	{
		case EMultiplexOpCode::Copy:
		{
			return (uint64)sizeof(FMultiplexCopyOp);
		}
		case EMultiplexOpCode::Increment:
		{
			return (uint64)sizeof(FMultiplexIncrementOp);
		}
		case EMultiplexOpCode::Decrement:
		{
			return (uint64)sizeof(FMultiplexDecrementOp);
		}
		case EMultiplexOpCode::Equals:
		{
			return (uint64)sizeof(FMultiplexEqualsOp);
		}
		case EMultiplexOpCode::NotEquals:
		{
			return (uint64)sizeof(FMultiplexNotEqualsOp);
		}
		case EMultiplexOpCode::Jump:
		{
			return (uint64)sizeof(FMultiplexJumpOp);
		}
		case EMultiplexOpCode::JumpIfTrue:
		{
			return (uint64)sizeof(FMultiplexJumpIfTrueOp);
		}
		case EMultiplexOpCode::JumpIfFalse:
		{
			return (uint64)sizeof(FMultiplexJumpIfFalseOp);
		}
		case EMultiplexOpCode::Execute:
		{
			uint64 NumBytes = (uint64)sizeof(FMultiplexExecuteOp);
			if(bIncludeArguments)
			{
				const FMultiplexExecuteOp& ExecuteOp = GetOpAt<FMultiplexExecuteOp>(InByteCodeIndex);
				NumBytes += (uint64)ExecuteOp.ArgumentCount * (uint64)sizeof(FMultiplexArgument);
			}
			return NumBytes;
		}
		case EMultiplexOpCode::Exit:
		{
			return (uint64)sizeof(FMultiplexExitOp);
		}
		case EMultiplexOpCode::Invalid:
		{
			ensure(false);
			return 0;
		}
	}
	return 0;
}

uint64 FMultiplexByteCode::AddCopyOp(const FMultiplexArgument& InSource, const FMultiplexArgument& InTarget, int32 InSourceOffset, int32 InTargetOffset, int32 InNumBytes)
{
	FMultiplexCopyOp Op(InSource, InTarget, InSourceOffset, InTargetOffset, InNumBytes);
	return AddOp(Op);
}

uint64 FMultiplexByteCode::AddIncrementOp(const FMultiplexArgument& InArg)
{
	FMultiplexIncrementOp Op(InArg);
	return AddOp(Op);
}

uint64 FMultiplexByteCode::AddDecrementOp(const FMultiplexArgument& InArg)
{
	FMultiplexDecrementOp Op(InArg);
	return AddOp(Op);
}

uint64 FMultiplexByteCode::AddEqualsOp(const FMultiplexArgument& InA, const FMultiplexArgument& InB, const FMultiplexArgument& InResult)
{
	FMultiplexEqualsOp Op(InA, InB, InResult);
	return AddOp(Op);
}

uint64 FMultiplexByteCode::AddNotEqualsOp(const FMultiplexArgument& InA, const FMultiplexArgument& InB, const FMultiplexArgument& InResult)
{
	FMultiplexNotEqualsOp Op(InA, InB, InResult);
	return AddOp(Op);
}

uint64 FMultiplexByteCode::AddJumpOp(uint64 InByteCodeIndex)
{
	FMultiplexJumpOp Op(InByteCodeIndex);
	return AddOp(Op);
}

uint64 FMultiplexByteCode::AddJumpIfTrueOp(uint64 InByteCodeIndex, const FMultiplexArgument& InCondition)
{
	FMultiplexJumpIfTrueOp Op(InByteCodeIndex, InCondition);
	return AddOp(Op);
}

uint64 FMultiplexByteCode::AddJumpIfFalseOp(uint64 InByteCodeIndex, const FMultiplexArgument& InCondition)
{
	FMultiplexJumpIfFalseOp Op(InByteCodeIndex, InCondition);
	return AddOp(Op);
}

uint64 FMultiplexByteCode::AddExecuteOp(uint16 InFunctionIndex, const TArrayView<FMultiplexArgument>& InArguments)
{
	FMultiplexExecuteOp Op(InFunctionIndex, (uint16)InArguments.Num());
	uint64 OpAddress = AddOp(Op);
	uint64 ArgumentsAddress = (uint64)ByteCode.AddUninitialized(sizeof(FMultiplexArgument) * InArguments.Num());
	FMemory::Memcpy(ByteCode.GetData() + ArgumentsAddress, InArguments.GetData(), sizeof(FMultiplexArgument) * InArguments.Num());
	return OpAddress;
}

uint64 FMultiplexByteCode::AddExitOp()
{
	FMultiplexExitOp Op;
	return AddOp(Op);
}

