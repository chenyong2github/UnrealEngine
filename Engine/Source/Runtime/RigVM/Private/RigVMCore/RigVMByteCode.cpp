// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVMByteCode.h"

FRigVMInstructionArray::FRigVMInstructionArray()
{
}

FRigVMInstructionArray::FRigVMInstructionArray(const FRigVMByteCode& InByteCode)
{
	uint64 ByteIndex = 0;
	while (ByteIndex < InByteCode.Num())
	{
		ERigVMOpCode OpCode = InByteCode.GetOpCodeAt(ByteIndex);
		Instructions.Add(FRigVMInstruction(OpCode, ByteIndex));
		ByteIndex += InByteCode.GetOpNumBytesAt(ByteIndex);
	}
}

void FRigVMInstructionArray::Reset()
{
	Instructions.Reset();
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
		case ERigVMOpCode::Execute_0_Args:
		case ERigVMOpCode::Execute_1_Args:
		case ERigVMOpCode::Execute_2_Args:
		case ERigVMOpCode::Execute_3_Args:
		case ERigVMOpCode::Execute_4_Args:
		case ERigVMOpCode::Execute_5_Args:
		case ERigVMOpCode::Execute_6_Args:
		case ERigVMOpCode::Execute_7_Args:
		case ERigVMOpCode::Execute_8_Args:
		case ERigVMOpCode::Execute_9_Args:
		case ERigVMOpCode::Execute_10_Args:
		case ERigVMOpCode::Execute_11_Args:
		case ERigVMOpCode::Execute_12_Args:
		case ERigVMOpCode::Execute_13_Args:
		case ERigVMOpCode::Execute_14_Args:
		case ERigVMOpCode::Execute_15_Args:
		case ERigVMOpCode::Execute_16_Args:
		case ERigVMOpCode::Execute_17_Args:
		case ERigVMOpCode::Execute_18_Args:
		case ERigVMOpCode::Execute_19_Args:
		case ERigVMOpCode::Execute_20_Args:
		case ERigVMOpCode::Execute_21_Args:
		case ERigVMOpCode::Execute_22_Args:
		case ERigVMOpCode::Execute_23_Args:
		case ERigVMOpCode::Execute_24_Args:
		case ERigVMOpCode::Execute_25_Args:
		case ERigVMOpCode::Execute_26_Args:
		case ERigVMOpCode::Execute_27_Args:
		case ERigVMOpCode::Execute_28_Args:
		case ERigVMOpCode::Execute_29_Args:
		case ERigVMOpCode::Execute_30_Args:
		case ERigVMOpCode::Execute_31_Args:
		case ERigVMOpCode::Execute_32_Args:
		case ERigVMOpCode::Execute_33_Args:
		case ERigVMOpCode::Execute_34_Args:
		case ERigVMOpCode::Execute_35_Args:
		case ERigVMOpCode::Execute_36_Args:
		case ERigVMOpCode::Execute_37_Args:
		case ERigVMOpCode::Execute_38_Args:
		case ERigVMOpCode::Execute_39_Args:
		case ERigVMOpCode::Execute_40_Args:
		case ERigVMOpCode::Execute_41_Args:
		case ERigVMOpCode::Execute_42_Args:
		case ERigVMOpCode::Execute_43_Args:
		case ERigVMOpCode::Execute_44_Args:
		case ERigVMOpCode::Execute_45_Args:
		case ERigVMOpCode::Execute_46_Args:
		case ERigVMOpCode::Execute_47_Args:
		case ERigVMOpCode::Execute_48_Args:
		case ERigVMOpCode::Execute_49_Args:
		case ERigVMOpCode::Execute_50_Args:
		case ERigVMOpCode::Execute_51_Args:
		case ERigVMOpCode::Execute_52_Args:
		case ERigVMOpCode::Execute_53_Args:
		case ERigVMOpCode::Execute_54_Args:
		case ERigVMOpCode::Execute_55_Args:
		case ERigVMOpCode::Execute_56_Args:
		case ERigVMOpCode::Execute_57_Args:
		case ERigVMOpCode::Execute_58_Args:
		case ERigVMOpCode::Execute_59_Args:
		case ERigVMOpCode::Execute_60_Args:
		case ERigVMOpCode::Execute_61_Args:
		case ERigVMOpCode::Execute_62_Args:
		case ERigVMOpCode::Execute_63_Args:
		case ERigVMOpCode::Execute_64_Args:
		{
			uint64 NumBytes = (uint64)sizeof(FRigVMExecuteOp);
			if(bIncludeArguments)
			{
				const FRigVMExecuteOp& ExecuteOp = GetOpAt<FRigVMExecuteOp>(InByteCodeIndex);
				NumBytes += (uint64)ExecuteOp.GetArgumentCount() * (uint64)sizeof(FRigVMArgument);
			}
			return NumBytes;
		}
		case ERigVMOpCode::Copy:
		{
			return (uint64)sizeof(FRigVMCopyOp);
		}
		case ERigVMOpCode::Zero:
		{
			return (uint64)sizeof(FRigVMZeroOp);
		}
		case ERigVMOpCode::BoolFalse:
		{
			return (uint64)sizeof(FRigVMFalseOp);
		}
		case ERigVMOpCode::BoolTrue:
		{
			return (uint64)sizeof(FRigVMTrueOp);
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
		case ERigVMOpCode::JumpAbsolute:
		case ERigVMOpCode::JumpForward:
		case ERigVMOpCode::JumpBackward:
		{
			return (uint64)sizeof(FRigVMJumpOp);
		}
		case ERigVMOpCode::JumpAbsoluteIf:
		case ERigVMOpCode::JumpForwardIf:
		case ERigVMOpCode::JumpBackwardIf:
		{
			return (uint64)sizeof(FRigVMJumpIfOp);
		}
		case ERigVMOpCode::ChangeType:
		{
			return (uint64)sizeof(FRigVMChangeTypeOp);
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

uint64 FRigVMByteCode::AddZeroOp(const FRigVMArgument& InArg)
{
	FRigVMZeroOp Op(InArg);
	return AddOp(Op);
}

uint64 FRigVMByteCode::AddFalseOp(const FRigVMArgument& InArg)
{
	FRigVMFalseOp Op(InArg);
	return AddOp(Op);
}

uint64 FRigVMByteCode::AddTrueOp(const FRigVMArgument& InArg)
{
	FRigVMTrueOp Op(InArg);
	return AddOp(Op);
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

uint64 FRigVMByteCode::AddJumpOp(ERigVMOpCode InOpCode, uint16 InInstructionIndex)
{
	FRigVMJumpOp Op(InOpCode, InInstructionIndex);
	return AddOp(Op);
}

uint64 FRigVMByteCode::AddJumpIfOp(ERigVMOpCode InOpCode, uint16 InInstructionIndex, const FRigVMArgument& InConditionArg, bool bJumpWhenConditionIs)
{
	FRigVMJumpIfOp Op(InOpCode, InInstructionIndex, InConditionArg, bJumpWhenConditionIs);
	return AddOp(Op);
}

uint64 FRigVMByteCode::AddChangeTypeOp(FRigVMArgument InArg, ERigVMRegisterType InType, uint16 InElementSize, uint16 InElementCount, uint16 InSliceCount)
{
	FRigVMChangeTypeOp Op(InArg, InType, InElementSize, InElementCount, InSliceCount);
	return AddOp(Op);
}

uint64 FRigVMByteCode::AddExecuteOp(uint16 InFunctionIndex, const TArrayView<FRigVMArgument>& InArguments)
{
	FRigVMExecuteOp Op(InFunctionIndex, (uint8)InArguments.Num());
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

