// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MultiplexVM.h"

void UMultiplexVM::Reset()
{
	Literals.Reset();
	WorkState.Reset();
	FunctionNames.Reset();
	Functions.Reset();
	ByteCode.Reset();
	Instructions.Reset();
}

int32 UMultiplexVM::AddMultiplexFunction(UScriptStruct* InMultiplexStruct, const FName& InFunctionName)
{
	check(InMultiplexStruct);
	FString FunctionKey = FString::Printf(TEXT("F%s::%s"), *InMultiplexStruct->GetName(), *InFunctionName.ToString());
	int32 FunctionIndex = FunctionNames.Find(FunctionKey);
	if (FunctionIndex != INDEX_NONE)
	{
		return FunctionIndex;
	}

	FMultiplexFunctionPtr Function = FMultiplexRegistry::Get().Find(*FunctionKey);
	if (Function == nullptr)
	{
		return INDEX_NONE;
	}

	FunctionNames.Add(FunctionKey);
	return Functions.Add(Function);
}

void UMultiplexVM::ResolveFunctionsIfRequired()
{
	if (Functions.Num() != FunctionNames.Num())
	{
		Functions.Reset();
		Functions.SetNumZeroed(FunctionNames.Num());

		for (int32 FunctionIndex = 0; FunctionIndex < FunctionNames.Num(); FunctionIndex)
		{
			Functions[FunctionIndex] = FMultiplexRegistry::Get().Find(*FunctionNames[FunctionIndex]);
		}
	}
}

void UMultiplexVM::RefreshInstructionsIfRequired()
{
	if (Instructions.Num() == 0)
	{
		Instructions = ByteCode.GetTable();
	}
}

bool UMultiplexVM::Execute(FMultiplexStorage** Storage, TArrayView<void*> AdditionalArgs)
{
	ResolveFunctionsIfRequired();
	RefreshInstructionsIfRequired();

	if (Instructions.Num() == 0)
	{
		return true;
	}

	FMultiplexStorage* LocalStorage[] = { &WorkState, &Literals }; 
	if (Storage == nullptr)
	{
		Storage = LocalStorage;
	}

	int32 InstructionIndex = 0;
	while (Instructions[InstructionIndex].OpCode != EMultiplexOpCode::Exit)
	{
		switch (Instructions[InstructionIndex].OpCode)
		{
			case EMultiplexOpCode::Copy:
			{
				const FMultiplexCopyOp& Op = ByteCode.GetOpAt<FMultiplexCopyOp>(Instructions[InstructionIndex]);
				Storage[0]->Copy(Op.Source.Index(), Op.Target.Index(), Storage[Op.Source.StorageType()], Op.SourceOffset, Op.TargetOffset, Op.NumBytes);
				InstructionIndex++;
				break;
			}
			case EMultiplexOpCode::Increment:
			{
				const FMultiplexIncrementOp& Op = ByteCode.GetOpAt<FMultiplexIncrementOp>(Instructions[InstructionIndex]);
				Storage[0]->GetRef<int32>(Op.Arg.Index())++;
				InstructionIndex++;
				break;
			}
			case EMultiplexOpCode::Decrement:
			{
				const FMultiplexDecrementOp& Op = ByteCode.GetOpAt<FMultiplexDecrementOp>(Instructions[InstructionIndex]);
				Storage[0]->GetRef<int32>(Op.Arg.Index())--;
				InstructionIndex++;
				break;
			}
			case EMultiplexOpCode::Equals:
			{
				const FMultiplexEqualsOp& Op = ByteCode.GetOpAt<FMultiplexEqualsOp>(Instructions[InstructionIndex]);
				ensureMsgf(false, TEXT("TO BE IMPLEMENTED"));
				break;
			}
			case EMultiplexOpCode::NotEquals:
			{
				const FMultiplexNotEqualsOp& Op = ByteCode.GetOpAt<FMultiplexNotEqualsOp>(Instructions[InstructionIndex]);
				ensureMsgf(false, TEXT("TO BE IMPLEMENTED"));
				break;
			}
			case EMultiplexOpCode::Jump:
			{
				const FMultiplexJumpOp& Op = ByteCode.GetOpAt<FMultiplexJumpOp>(Instructions[InstructionIndex]);
				ensureMsgf(false, TEXT("TO BE IMPLEMENTED"));
				break;
			}
			case EMultiplexOpCode::JumpIfTrue:
			{
				const FMultiplexJumpIfTrueOp& Op = ByteCode.GetOpAt<FMultiplexJumpIfTrueOp>(Instructions[InstructionIndex]);
				ensureMsgf(false, TEXT("TO BE IMPLEMENTED"));
				break;
			}
			case EMultiplexOpCode::JumpIfFalse:
			{
				const FMultiplexJumpIfFalseOp& Op = ByteCode.GetOpAt<FMultiplexJumpIfFalseOp>(Instructions[InstructionIndex]);
				ensureMsgf(false, TEXT("TO BE IMPLEMENTED"));
				break;
			}
			case EMultiplexOpCode::Execute:
			{
				const FMultiplexExecuteOp& Op = ByteCode.GetOpAt<FMultiplexExecuteOp>(Instructions[InstructionIndex]);
				TArrayView<FMultiplexArgument> Args = ByteCode.GetArgumentsForExecuteOp(Instructions[InstructionIndex]);
				(*Functions[Op.FunctionIndex])(Args, Storage, AdditionalArgs);
				InstructionIndex++;
				break;
			}
			case EMultiplexOpCode::Exit:
			case EMultiplexOpCode::Invalid:
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
