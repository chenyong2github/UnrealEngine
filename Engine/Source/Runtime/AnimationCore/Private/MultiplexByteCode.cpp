// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MultiplexByteCode.h"

FMultiplexByteCode::FMultiplexByteCode()
{
}

void FMultiplexByteCode::Reset()
{
	ByteCode.Reset();
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

