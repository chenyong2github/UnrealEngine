// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVM.h"

URigVM::URigVM()
{
	WorkStorage.SetStorageType(ERigVMStorageType::Work);
	LiteralStorage.SetStorageType(ERigVMStorageType::Literal);
}

URigVM::~URigVM()
{
	Reset();
}

void URigVM::Reset()
{
	WorkStorage.Reset();
	LiteralStorage.Reset();
	FunctionNames.Reset();
	Functions.Reset();
	ByteCode.Reset();
	Instructions.Reset();
}

int32 URigVM::AddRigVMFunction(UScriptStruct* InRigVMStruct, const FName& InFunctionName)
{
	check(InRigVMStruct);
	FString FunctionKey = FString::Printf(TEXT("F%s::%s"), *InRigVMStruct->GetName(), *InFunctionName.ToString());
	int32 FunctionIndex = FunctionNames.Find(FunctionKey);
	if (FunctionIndex != INDEX_NONE)
	{
		return FunctionIndex;
	}

	FRigVMFunctionPtr Function = FRigVMRegistry::Get().Find(*FunctionKey);
	if (Function == nullptr)
	{
		return INDEX_NONE;
	}

	FunctionNames.Add(FunctionKey);
	return Functions.Add(Function);
}

void URigVM::ResolveFunctionsIfRequired()
{
	if (Functions.Num() != FunctionNames.Num())
	{
		Functions.Reset();
		Functions.SetNumZeroed(FunctionNames.Num());

		for (int32 FunctionIndex = 0; FunctionIndex < FunctionNames.Num(); FunctionIndex)
		{
			Functions[FunctionIndex] = FRigVMRegistry::Get().Find(*FunctionNames[FunctionIndex]);
		}
	}
}

void URigVM::RefreshInstructionsIfRequired()
{
	if (Instructions.Num() == 0)
	{
		Instructions = ByteCode.GetTable();
	}
}

bool URigVM::Execute(FRigVMStoragePtrArray Storage, TArrayView<void*> AdditionalArgs)
{
	ResolveFunctionsIfRequired();
	RefreshInstructionsIfRequired();

	if (Instructions.Num() == 0)
	{
		return true;
	}

	FRigVMStorage* LocalStorage[] = { &WorkStorage, &LiteralStorage }; 
	if (Storage.Num() == 0)
	{
		Storage = FRigVMStoragePtrArray(LocalStorage, 2);
	}

	int32 InstructionIndex = 0;
	while (Instructions[InstructionIndex].OpCode != ERigVMOpCode::Exit)
	{
		switch (Instructions[InstructionIndex].OpCode)
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
				const FRigVMExecuteOp& Op = ByteCode.GetOpAt<FRigVMExecuteOp>(Instructions[InstructionIndex]);
				FRigVMArgumentArray Args = ByteCode.GetArgumentsForExecuteOp(Instructions[InstructionIndex]);
				(*Functions[Op.FunctionIndex])(Args, Storage, AdditionalArgs);
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Copy:
			{
				const FRigVMCopyOp& Op = ByteCode.GetOpAt<FRigVMCopyOp>(Instructions[InstructionIndex]);
				Storage[0]->Copy(Op.Source.GetRegisterIndex(), Op.Target.GetRegisterIndex(), Storage[Op.Source.GetStorageIndex()], Op.SourceOffset, Op.TargetOffset, Op.NumBytes);
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Increment:
			{
				const FRigVMIncrementOp& Op = ByteCode.GetOpAt<FRigVMIncrementOp>(Instructions[InstructionIndex]);
				Storage[0]->GetRef<int32>(Op.Arg.GetRegisterIndex())++;
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Decrement:
			{
				const FRigVMDecrementOp& Op = ByteCode.GetOpAt<FRigVMDecrementOp>(Instructions[InstructionIndex]);
				Storage[0]->GetRef<int32>(Op.Arg.GetRegisterIndex())--;
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Equals:
			{
				const FRigVMEqualsOp& Op = ByteCode.GetOpAt<FRigVMEqualsOp>(Instructions[InstructionIndex]);
				ensureMsgf(false, TEXT("TO BE IMPLEMENTED"));
				break;
			}
			case ERigVMOpCode::NotEquals:
			{
				const FRigVMNotEqualsOp& Op = ByteCode.GetOpAt<FRigVMNotEqualsOp>(Instructions[InstructionIndex]);
				ensureMsgf(false, TEXT("TO BE IMPLEMENTED"));
				break;
			}
			case ERigVMOpCode::Jump:
			{
				const FRigVMJumpOp& Op = ByteCode.GetOpAt<FRigVMJumpOp>(Instructions[InstructionIndex]);
				ensureMsgf(false, TEXT("TO BE IMPLEMENTED"));
				break;
			}
			case ERigVMOpCode::JumpIfTrue:
			{
				const FRigVMJumpIfTrueOp& Op = ByteCode.GetOpAt<FRigVMJumpIfTrueOp>(Instructions[InstructionIndex]);
				ensureMsgf(false, TEXT("TO BE IMPLEMENTED"));
				break;
			}
			case ERigVMOpCode::JumpIfFalse:
			{
				const FRigVMJumpIfFalseOp& Op = ByteCode.GetOpAt<FRigVMJumpIfFalseOp>(Instructions[InstructionIndex]);
				ensureMsgf(false, TEXT("TO BE IMPLEMENTED"));
				break;
			}
			case ERigVMOpCode::Exit:
			case ERigVMOpCode::Invalid:
			{
				ensure(false);
				return false;
			}
		}

		if (!Instructions.IsValidIndex(InstructionIndex))
		{
			break;
		}
	}

	return true;
}
