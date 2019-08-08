// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVM.h"

URigVM::URigVM()
{
	WorkMemory.SetMemoryType(ERigVMMemoryType::Work);
	LiteralMemory.SetMemoryType(ERigVMMemoryType::Literal);
}

URigVM::~URigVM()
{
	Reset();
}

void URigVM::Reset()
{
	WorkMemory.Reset();
	LiteralMemory.Reset();
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

		for (int32 FunctionIndex = 0; FunctionIndex < FunctionNames.Num(); FunctionIndex++)
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

bool URigVM::Execute(FRigVMMemoryContainerPtrArray Memory, TArrayView<void*> AdditionalArgs)
{
	ResolveFunctionsIfRequired();
	RefreshInstructionsIfRequired();

	if (Instructions.Num() == 0)
	{
		return true;
	}

	FRigVMMemoryContainer* LocalMemory[] = { &WorkMemory, &LiteralMemory }; 
	if (Memory.Num() == 0)
	{
		Memory = FRigVMMemoryContainerPtrArray(LocalMemory, 2);
	}

	uint16 InstructionIndex = 0;
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
				(*Functions[Op.FunctionIndex])(Args, Memory, AdditionalArgs);
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Zero:
			{
				const FRigVMZeroOp& Op = ByteCode.GetOpAt<FRigVMZeroOp>(Instructions[InstructionIndex]);
				Memory[Op.Arg.GetContainerIndex()]->GetRef<int32>(Op.Arg.GetRegisterIndex()) = 0;
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::BoolFalse:
			{
				const FRigVMFalseOp& Op = ByteCode.GetOpAt<FRigVMFalseOp>(Instructions[InstructionIndex]);
				Memory[Op.Arg.GetContainerIndex()]->GetRef<bool>(Op.Arg.GetRegisterIndex()) = false;
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::BoolTrue:
			{
				const FRigVMTrueOp& Op = ByteCode.GetOpAt<FRigVMTrueOp>(Instructions[InstructionIndex]);
				Memory[Op.Arg.GetContainerIndex()]->GetRef<bool>(Op.Arg.GetRegisterIndex()) = false;
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Copy:
			{
				const FRigVMCopyOp& Op = ByteCode.GetOpAt<FRigVMCopyOp>(Instructions[InstructionIndex]);
				Memory[Op.Target.GetContainerIndex()]->Copy(Op.Source.GetRegisterIndex(), Op.Target.GetRegisterIndex(), Memory[Op.Source.GetContainerIndex()], Op.SourceOffset, Op.TargetOffset, Op.NumBytes);
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Increment:
			{
				const FRigVMIncrementOp& Op = ByteCode.GetOpAt<FRigVMIncrementOp>(Instructions[InstructionIndex]);
				Memory[Op.Arg.GetContainerIndex()]->GetRef<int32>(Op.Arg.GetRegisterIndex())++;
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Decrement:
			{
				const FRigVMDecrementOp& Op = ByteCode.GetOpAt<FRigVMDecrementOp>(Instructions[InstructionIndex]);
				Memory[Op.Arg.GetContainerIndex()]->GetRef<int32>(Op.Arg.GetRegisterIndex())--;
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Equals:
			case ERigVMOpCode::NotEquals:
			{
				const FRigVMEqualsOp& Op = ByteCode.GetOpAt<FRigVMEqualsOp>(Instructions[InstructionIndex]);
				const FRigVMRegister& RegisterA = (*Memory[Op.A.GetContainerIndex()])[Op.A.GetRegisterIndex()];
				const FRigVMRegister& RegisterB = (*Memory[Op.B.GetContainerIndex()])[Op.B.GetRegisterIndex()];
				uint16 BytesA = RegisterA.GetNumBytesPerSlice();
				uint16 BytesB = RegisterB.GetNumBytesPerSlice();
				
				bool Result = false;
				if (BytesA == BytesB && RegisterA.Type == RegisterB.Type && RegisterA.ScriptStructIndex == RegisterB.ScriptStructIndex)
				{
					switch (RegisterA.Type)
					{
						case ERigVMRegisterType::Plain:
						case ERigVMRegisterType::Name:
						{
							void * DataA = Memory[Op.A.GetContainerIndex()]->GetData(Op.A.GetRegisterIndex());
							void * DataB = Memory[Op.B.GetContainerIndex()]->GetData(Op.B.GetRegisterIndex());
							Result = FMemory::Memcmp(DataA, DataB, BytesA) == 0;
							break;
						}
						case ERigVMRegisterType::String:
						{
							TArrayView<FString> StringsA = Memory[Op.A.GetContainerIndex()]->GetArray<FString>(Op.A.GetRegisterIndex());
							TArrayView<FString> StringsB = Memory[Op.B.GetContainerIndex()]->GetArray<FString>(Op.B.GetRegisterIndex());

							Result = true;
							for (int32 StringIndex = 0; StringIndex < StringsA.Num(); StringIndex++)
							{
								if (StringsA[StringIndex] != StringsB[StringIndex])
								{
									Result = false;
									break;
								}
							}
							break;
						}
						case ERigVMRegisterType::Struct:
						{
							UScriptStruct* ScriptStruct = Memory[Op.A.GetContainerIndex()]->GetScriptStruct(RegisterA.ScriptStructIndex);

							uint8* DataA = (uint8*)Memory[Op.A.GetContainerIndex()]->GetData(Op.A.GetRegisterIndex());
							uint8* DataB = (uint8*)Memory[Op.B.GetContainerIndex()]->GetData(Op.B.GetRegisterIndex());
							
							Result = true;
							for (int32 ElementIndex = 0; ElementIndex < RegisterA.ElementCount; ElementIndex++)
							{
								if (!ScriptStruct->CompareScriptStruct(DataA, DataB, 0))
								{
									Result = false;
									break;
								}
								DataA += RegisterA.ElementSize;
								DataB += RegisterB.ElementSize;
							}

							break;
						}
						case ERigVMRegisterType::Invalid:
						{
							break;
						}
					}
				}
				if (Instructions[InstructionIndex].OpCode == ERigVMOpCode::NotEquals)
				{
					Result = !Result;
				}
				Memory[Op.Result.GetContainerIndex()]->GetRef<bool>(Op.Result.GetRegisterIndex()) = Result;
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::JumpAbsolute:
			{
				const FRigVMJumpOp& Op = ByteCode.GetOpAt<FRigVMJumpOp>(Instructions[InstructionIndex]);
				InstructionIndex = Op.InstructionIndex;
				break;
			}
			case ERigVMOpCode::JumpForward:
			{
				const FRigVMJumpOp& Op = ByteCode.GetOpAt<FRigVMJumpOp>(Instructions[InstructionIndex]);
				InstructionIndex += Op.InstructionIndex;
				break;
			}
			case ERigVMOpCode::JumpBackward:
			{
				const FRigVMJumpOp& Op = ByteCode.GetOpAt<FRigVMJumpOp>(Instructions[InstructionIndex]);
				InstructionIndex -= Op.InstructionIndex;
				break;
			}
			case ERigVMOpCode::JumpAbsoluteIf:
			{
				const FRigVMJumpIfOp& Op = ByteCode.GetOpAt<FRigVMJumpIfOp>(Instructions[InstructionIndex]);
				const bool Condition = Memory[0]->GetRef<bool>(Op.ConditionArg.GetRegisterIndex());
				if (Condition == Op.Condition)
				{
					InstructionIndex = Op.InstructionIndex;
				}
				else
				{
					InstructionIndex++;
				}
				break;
			}
			case ERigVMOpCode::JumpForwardIf:
			{
				const FRigVMJumpIfOp& Op = ByteCode.GetOpAt<FRigVMJumpIfOp>(Instructions[InstructionIndex]);
				const bool Condition = Memory[0]->GetRef<bool>(Op.ConditionArg.GetRegisterIndex());
				if (Condition == Op.Condition)
				{
					InstructionIndex += Op.InstructionIndex;
				}
				else
				{
					InstructionIndex++;
				}
				break;
			}
			case ERigVMOpCode::JumpBackwardIf:
			{
				const FRigVMJumpIfOp& Op = ByteCode.GetOpAt<FRigVMJumpIfOp>(Instructions[InstructionIndex]);
				const bool Condition = Memory[0]->GetRef<bool>(Op.ConditionArg.GetRegisterIndex());
				if (Condition == Op.Condition)
				{
					InstructionIndex -= Op.InstructionIndex;
				}
				else
				{
					InstructionIndex++;
				}
				break;
			}
			case ERigVMOpCode::ChangeType:
			{
				const FRigVMChangeTypeOp& Op = ByteCode.GetOpAt<FRigVMChangeTypeOp>(Instructions[InstructionIndex]);
				Memory[Op.Arg.GetContainerIndex()]->ChangeRegisterType(Op.Arg.GetRegisterIndex(), Op.Type, Op.ElementSize, nullptr, Op.ElementCount, Op.SliceCount);
				InstructionIndex++;
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
