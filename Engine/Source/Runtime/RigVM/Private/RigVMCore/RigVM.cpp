// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVM.h"
#include "UObject/Package.h"
#include "UObject/AnimObjectVersion.h"
#include "HAL/PlatformTLS.h"

bool FRigVMParameter::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FAnimObjectVersion::GUID);

	if (Ar.CustomVer(FAnimObjectVersion::GUID) < FAnimObjectVersion::StoreMarkerNamesOnSkeleton)
	{
		return false;
	}

	Ar << Type;
	Ar << Name;
	Ar << RegisterIndex;
	Ar << CPPType;
	Ar << ScriptStructPath;

	if (Ar.IsLoading())
	{
		ScriptStruct = nullptr;
	}

	return true;
}

UScriptStruct* FRigVMParameter::GetScriptStruct() const
{
	if (ScriptStruct == nullptr)
	{
		if (ScriptStructPath != NAME_None)
		{
			FRigVMParameter* MutableThis = (FRigVMParameter*)this;
			MutableThis->ScriptStruct = FindObject<UScriptStruct>(ANY_PACKAGE, *ScriptStructPath.ToString());
		}
	}
	return ScriptStruct;
}

URigVM::URigVM()
	: ThreadId(INDEX_NONE)
{
	WorkMemory.SetMemoryType(ERigVMMemoryType::Work);
	LiteralMemory.SetMemoryType(ERigVMMemoryType::Literal);
}

URigVM::~URigVM()
{
	Reset();
}

void URigVM::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FAnimObjectVersion::GUID);

	if (Ar.CustomVer(FAnimObjectVersion::GUID) < FAnimObjectVersion::StoreMarkerNamesOnSkeleton)
	{
		return;
	}

	if (Ar.IsLoading())
	{
		Reset();
	}

	Ar << WorkMemory;
	Ar << LiteralMemory;
	Ar << FunctionNames;
	Ar << ByteCode;
	Ar << Parameters;

	if (Ar.IsLoading())
	{
		if (WorkMemory.bEncounteredErrorDuringLoad ||
			LiteralMemory.bEncounteredErrorDuringLoad)
		{
			Reset();
		}
		else
		{
			Instructions.Reset();
			Functions.Reset();
			ParametersNameMap.Reset();

			for (int32 Index = 0; Index < Parameters.Num(); Index++)
			{
				ParametersNameMap.Add(Parameters[Index].Name, Index);
			}

			InvalidateCachedMemory();
		}
	}
}

void URigVM::Reset()
{
	WorkMemory.Reset();
	LiteralMemory.Reset();
	FunctionNames.Reset();
	Functions.Reset();
	ByteCode.Reset();
	Instructions.Reset();
	Parameters.Reset();
	ParametersNameMap.Reset();

	InvalidateCachedMemory();
}

void URigVM::Empty()
{
	WorkMemory.Empty();
	LiteralMemory.Empty();
	FunctionNames.Empty();
	Functions.Empty();
	ByteCode.Empty();
	Instructions.Empty();
	Parameters.Empty();
	ParametersNameMap.Empty();

	InvalidateCachedMemory();

	CachedMemory.Empty();
	FirstPointerForInstruction.Empty();
	CachedMemoryPointers.Empty();
}

void URigVM::CopyFrom(URigVM* InVM)
{
	check(InVM);
	
	Reset();

	WorkMemory = InVM->WorkMemory;
	LiteralMemory = InVM->LiteralMemory;
	FunctionNames = InVM->FunctionNames;
	Functions = InVM->Functions;
	ByteCode = InVM->ByteCode;
	Instructions = InVM->Instructions;
	Parameters = InVM->Parameters;
	ParametersNameMap = InVM->ParametersNameMap;
}

int32 URigVM::AddRigVMFunction(UScriptStruct* InRigVMStruct, const FName& InMethodName)
{
	check(InRigVMStruct);
	FString FunctionKey = FString::Printf(TEXT("F%s::%s"), *InRigVMStruct->GetName(), *InMethodName.ToString());
	int32 FunctionIndex = FunctionNames.Find(*FunctionKey);
	if (FunctionIndex != INDEX_NONE)
	{
		return FunctionIndex;
	}

	FRigVMFunctionPtr Function = FRigVMRegistry::Get().Find(*FunctionKey);
	if (Function == nullptr)
	{
		return INDEX_NONE;
	}

	FunctionNames.Add(*FunctionKey);
	return Functions.Add(Function);
}

FString URigVM::GetRigVMFunctionName(int32 InFunctionIndex) const
{
	return FunctionNames[InFunctionIndex].ToString();
}

const FRigVMInstructionArray& URigVM::GetInstructions()
{
	RefreshInstructionsIfRequired();
	return Instructions;
}

const TArray<FRigVMParameter>& URigVM::GetParameters() const
{
	return Parameters;
}

FRigVMParameter URigVM::GetParameterByName(const FName& InParameterName)
{
	if (ParametersNameMap.Num() == Parameters.Num())
	{
		const int32* ParameterIndex = ParametersNameMap.Find(InParameterName);
		if (ParameterIndex)
		{
			Parameters[*ParameterIndex].GetScriptStruct();
			return Parameters[*ParameterIndex];
		}
		return FRigVMParameter();
	}

	for (FRigVMParameter& Parameter : Parameters)
	{
		if (Parameter.GetName() == InParameterName)
		{
			Parameter.GetScriptStruct();
			return Parameter;
		}
	}

	return FRigVMParameter();
}

void URigVM::ResolveFunctionsIfRequired()
{
	if (Functions.Num() != FunctionNames.Num())
	{
		Functions.Reset();
		Functions.SetNumZeroed(FunctionNames.Num());

		for (int32 FunctionIndex = 0; FunctionIndex < FunctionNames.Num(); FunctionIndex++)
		{
			Functions[FunctionIndex] = FRigVMRegistry::Get().Find(*FunctionNames[FunctionIndex].ToString());
		}
	}
}

void URigVM::RefreshInstructionsIfRequired()
{
	if (Instructions.Num() == 0)
	{
		Instructions = ByteCode.GetInstructions();
	}
}

void URigVM::InvalidateCachedMemory()
{
	CachedMemory.Reset();
	FirstPointerForInstruction.Reset();
	CachedMemoryPointers.Reset();
}

void URigVM::CacheMemoryPointersIfRequired(FRigVMMemoryContainerPtrArray InMemory)
{
	if (ThreadId != INDEX_NONE)
	{
		ensureMsgf(ThreadId == FPlatformTLS::GetCurrentThreadId(), TEXT("RigVM from multiple threads (%d and %d)"), ThreadId, (int32)FPlatformTLS::GetCurrentThreadId());
	}
	TGuardValue<int32> GuardThreadId(ThreadId, FPlatformTLS::GetCurrentThreadId());

	RefreshInstructionsIfRequired();

	if (Instructions.Num() == 0 || InMemory.Num() == 0)
	{
		InvalidateCachedMemory();
		return;
	}

	if (Instructions.Num() != FirstPointerForInstruction.Num())
	{
		InvalidateCachedMemory();
	}
	else if (InMemory.Num() != CachedMemory.Num())
	{
		InvalidateCachedMemory();
	}
	else
	{
		for (int32 Index = 0; Index < InMemory.Num(); Index++)
		{
			if (InMemory[Index] != CachedMemory[Index])
			{
				InvalidateCachedMemory();
				break;
			}
		}
	}

	if (Instructions.Num() == FirstPointerForInstruction.Num())
	{
		return;
	}

	for (int32 Index = 0; Index < InMemory.Num(); Index++)
	{
		CachedMemory.Add(InMemory[Index]);
	}

	uint16 InstructionIndex = 0;
	while (Instructions[InstructionIndex].OpCode != ERigVMOpCode::Exit)
	{
		FirstPointerForInstruction.Add(CachedMemoryPointers.Num());

		switch (Instructions[InstructionIndex].OpCode)
		{
			case ERigVMOpCode::Execute_0_Operands:
			case ERigVMOpCode::Execute_1_Operands:
			case ERigVMOpCode::Execute_2_Operands:
			case ERigVMOpCode::Execute_3_Operands:
			case ERigVMOpCode::Execute_4_Operands:
			case ERigVMOpCode::Execute_5_Operands:
			case ERigVMOpCode::Execute_6_Operands:
			case ERigVMOpCode::Execute_7_Operands:
			case ERigVMOpCode::Execute_8_Operands:
			case ERigVMOpCode::Execute_9_Operands:
			case ERigVMOpCode::Execute_10_Operands:
			case ERigVMOpCode::Execute_11_Operands:
			case ERigVMOpCode::Execute_12_Operands:
			case ERigVMOpCode::Execute_13_Operands:
			case ERigVMOpCode::Execute_14_Operands:
			case ERigVMOpCode::Execute_15_Operands:
			case ERigVMOpCode::Execute_16_Operands:
			case ERigVMOpCode::Execute_17_Operands:
			case ERigVMOpCode::Execute_18_Operands:
			case ERigVMOpCode::Execute_19_Operands:
			case ERigVMOpCode::Execute_20_Operands:
			case ERigVMOpCode::Execute_21_Operands:
			case ERigVMOpCode::Execute_22_Operands:
			case ERigVMOpCode::Execute_23_Operands:
			case ERigVMOpCode::Execute_24_Operands:
			case ERigVMOpCode::Execute_25_Operands:
			case ERigVMOpCode::Execute_26_Operands:
			case ERigVMOpCode::Execute_27_Operands:
			case ERigVMOpCode::Execute_28_Operands:
			case ERigVMOpCode::Execute_29_Operands:
			case ERigVMOpCode::Execute_30_Operands:
			case ERigVMOpCode::Execute_31_Operands:
			case ERigVMOpCode::Execute_32_Operands:
			case ERigVMOpCode::Execute_33_Operands:
			case ERigVMOpCode::Execute_34_Operands:
			case ERigVMOpCode::Execute_35_Operands:
			case ERigVMOpCode::Execute_36_Operands:
			case ERigVMOpCode::Execute_37_Operands:
			case ERigVMOpCode::Execute_38_Operands:
			case ERigVMOpCode::Execute_39_Operands:
			case ERigVMOpCode::Execute_40_Operands:
			case ERigVMOpCode::Execute_41_Operands:
			case ERigVMOpCode::Execute_42_Operands:
			case ERigVMOpCode::Execute_43_Operands:
			case ERigVMOpCode::Execute_44_Operands:
			case ERigVMOpCode::Execute_45_Operands:
			case ERigVMOpCode::Execute_46_Operands:
			case ERigVMOpCode::Execute_47_Operands:
			case ERigVMOpCode::Execute_48_Operands:
			case ERigVMOpCode::Execute_49_Operands:
			case ERigVMOpCode::Execute_50_Operands:
			case ERigVMOpCode::Execute_51_Operands:
			case ERigVMOpCode::Execute_52_Operands:
			case ERigVMOpCode::Execute_53_Operands:
			case ERigVMOpCode::Execute_54_Operands:
			case ERigVMOpCode::Execute_55_Operands:
			case ERigVMOpCode::Execute_56_Operands:
			case ERigVMOpCode::Execute_57_Operands:
			case ERigVMOpCode::Execute_58_Operands:
			case ERigVMOpCode::Execute_59_Operands:
			case ERigVMOpCode::Execute_60_Operands:
			case ERigVMOpCode::Execute_61_Operands:
			case ERigVMOpCode::Execute_62_Operands:
			case ERigVMOpCode::Execute_63_Operands:
			case ERigVMOpCode::Execute_64_Operands:
			{
				const FRigVMExecuteOp& Op = ByteCode.GetOpAt<FRigVMExecuteOp>(Instructions[InstructionIndex]);
				FRigVMOperandArray Operands = ByteCode.GetOperandsForExecuteOp(Instructions[InstructionIndex]);

				for (const FRigVMOperand& Arg : Operands)
				{
					void* Ptr = CachedMemory[Arg.GetContainerIndex()]->GetData(Arg.GetRegisterIndex(), Arg.GetRegisterOffset());
					CachedMemoryPointers.Add(Ptr);

					const FRigVMRegister& Register = CachedMemory[Arg.GetContainerIndex()]->GetRegister(Arg);
					if (Register.IsArray())
					{
						void* NumBytesAsPtr = reinterpret_cast<void*>(Register.ElementCount);
						CachedMemoryPointers.Add(NumBytesAsPtr);
					}
				}

				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Zero:
			case ERigVMOpCode::BoolFalse:
			case ERigVMOpCode::BoolTrue:
			case ERigVMOpCode::Increment:
			case ERigVMOpCode::Decrement:
			{
				const FRigVMUnaryOp& Op = ByteCode.GetOpAt<FRigVMUnaryOp>(Instructions[InstructionIndex]);
				const FRigVMOperand& Arg = Op.Arg;
				CachedMemoryPointers.Add(CachedMemory[Arg.GetContainerIndex()]->GetData(Arg.GetRegisterIndex(), Arg.GetRegisterOffset()));
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Copy:
			{
				const FRigVMCopyOp& Op = ByteCode.GetOpAt<FRigVMCopyOp>(Instructions[InstructionIndex]);
				FRigVMOperand Arg = Op.Source;
				CachedMemoryPointers.Add(CachedMemory[Arg.GetContainerIndex()]->GetData(Arg.GetRegisterIndex(), Arg.GetRegisterOffset()));
				Arg = Op.Target;
				CachedMemoryPointers.Add(CachedMemory[Arg.GetContainerIndex()]->GetData(Arg.GetRegisterIndex(), Arg.GetRegisterOffset()));

				const FRigVMRegister& TargetRegister = CachedMemory[Arg.GetContainerIndex()]->Registers[Arg.GetRegisterIndex()];
				uint16 NumBytes = TargetRegister.GetNumBytesPerSlice();

				ERigVMRegisterType TargetType = TargetRegister.Type;
				if (Arg.GetRegisterOffset() != INDEX_NONE)
				{
					TargetType = CachedMemory[Arg.GetContainerIndex()]->RegisterOffsets[Arg.GetRegisterOffset()].GetType();
					NumBytes = CachedMemory[Arg.GetContainerIndex()]->RegisterOffsets[Arg.GetRegisterOffset()].GetElementSize();
				}

				CachedMemoryPointers.Add(reinterpret_cast<void*>(NumBytes));
				CachedMemoryPointers.Add(reinterpret_cast<void*>((uint16)TargetType));

				if (TargetType == ERigVMRegisterType::Struct)
				{
					CachedMemoryPointers.Add(CachedMemory[Arg.GetContainerIndex()]->GetScriptStruct(Arg.GetRegisterIndex(), Arg.GetRegisterOffset()));
				}

				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Equals:
			case ERigVMOpCode::NotEquals:
			{
				const FRigVMComparisonOp& Op = ByteCode.GetOpAt<FRigVMComparisonOp>(Instructions[InstructionIndex]);
				FRigVMOperand Arg = Op.A;
				CachedMemoryPointers.Add(CachedMemory[Arg.GetContainerIndex()]->GetData(Arg.GetRegisterIndex(), Arg.GetRegisterOffset()));
				Arg = Op.B;
				CachedMemoryPointers.Add(CachedMemory[Arg.GetContainerIndex()]->GetData(Arg.GetRegisterIndex(), Arg.GetRegisterOffset()));
				Arg = Op.Result;
				CachedMemoryPointers.Add(CachedMemory[Arg.GetContainerIndex()]->GetData(Arg.GetRegisterIndex(), Arg.GetRegisterOffset()));
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::JumpAbsolute:
			case ERigVMOpCode::JumpForward:
			case ERigVMOpCode::JumpBackward:
			{
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::JumpAbsoluteIf:
			case ERigVMOpCode::JumpForwardIf:
			case ERigVMOpCode::JumpBackwardIf:
			{
				const FRigVMJumpIfOp& Op = ByteCode.GetOpAt<FRigVMJumpIfOp>(Instructions[InstructionIndex]);
				const FRigVMOperand& Arg = Op.Arg;
				CachedMemoryPointers.Add(CachedMemory[Arg.GetContainerIndex()]->GetData(Arg.GetRegisterIndex(), Arg.GetRegisterOffset()));
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::ChangeType:
			{
				const FRigVMChangeTypeOp& Op = ByteCode.GetOpAt<FRigVMChangeTypeOp>(Instructions[InstructionIndex]);
				const FRigVMOperand& Arg = Op.Arg;
				CachedMemoryPointers.Add(CachedMemory[Arg.GetContainerIndex()]->GetData(Arg.GetRegisterIndex(), Arg.GetRegisterOffset()));
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Exit:
			case ERigVMOpCode::Invalid:
			{
				ensure(false);
				break;
			}
		}

		if (!Instructions.IsValidIndex(InstructionIndex))
		{
			break;
		}
	}

	if (FirstPointerForInstruction.Num() < Instructions.Num())
	{
		FirstPointerForInstruction.Add(CachedMemoryPointers.Num());
	}
}

bool URigVM::Execute(FRigVMMemoryContainerPtrArray Memory, TArrayView<void*> AdditionalArguments)
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

	CacheMemoryPointersIfRequired(Memory);

	uint16 InstructionIndex = 0;
	while (Instructions[InstructionIndex].OpCode != ERigVMOpCode::Exit)
	{
		switch (Instructions[InstructionIndex].OpCode)
		{
			case ERigVMOpCode::Execute_0_Operands:
			case ERigVMOpCode::Execute_1_Operands:
			case ERigVMOpCode::Execute_2_Operands:
			case ERigVMOpCode::Execute_3_Operands:
			case ERigVMOpCode::Execute_4_Operands:
			case ERigVMOpCode::Execute_5_Operands:
			case ERigVMOpCode::Execute_6_Operands:
			case ERigVMOpCode::Execute_7_Operands:
			case ERigVMOpCode::Execute_8_Operands:
			case ERigVMOpCode::Execute_9_Operands:
			case ERigVMOpCode::Execute_10_Operands:
			case ERigVMOpCode::Execute_11_Operands:
			case ERigVMOpCode::Execute_12_Operands:
			case ERigVMOpCode::Execute_13_Operands:
			case ERigVMOpCode::Execute_14_Operands:
			case ERigVMOpCode::Execute_15_Operands:
			case ERigVMOpCode::Execute_16_Operands:
			case ERigVMOpCode::Execute_17_Operands:
			case ERigVMOpCode::Execute_18_Operands:
			case ERigVMOpCode::Execute_19_Operands:
			case ERigVMOpCode::Execute_20_Operands:
			case ERigVMOpCode::Execute_21_Operands:
			case ERigVMOpCode::Execute_22_Operands:
			case ERigVMOpCode::Execute_23_Operands:
			case ERigVMOpCode::Execute_24_Operands:
			case ERigVMOpCode::Execute_25_Operands:
			case ERigVMOpCode::Execute_26_Operands:
			case ERigVMOpCode::Execute_27_Operands:
			case ERigVMOpCode::Execute_28_Operands:
			case ERigVMOpCode::Execute_29_Operands:
			case ERigVMOpCode::Execute_30_Operands:
			case ERigVMOpCode::Execute_31_Operands:
			case ERigVMOpCode::Execute_32_Operands:
			case ERigVMOpCode::Execute_33_Operands:
			case ERigVMOpCode::Execute_34_Operands:
			case ERigVMOpCode::Execute_35_Operands:
			case ERigVMOpCode::Execute_36_Operands:
			case ERigVMOpCode::Execute_37_Operands:
			case ERigVMOpCode::Execute_38_Operands:
			case ERigVMOpCode::Execute_39_Operands:
			case ERigVMOpCode::Execute_40_Operands:
			case ERigVMOpCode::Execute_41_Operands:
			case ERigVMOpCode::Execute_42_Operands:
			case ERigVMOpCode::Execute_43_Operands:
			case ERigVMOpCode::Execute_44_Operands:
			case ERigVMOpCode::Execute_45_Operands:
			case ERigVMOpCode::Execute_46_Operands:
			case ERigVMOpCode::Execute_47_Operands:
			case ERigVMOpCode::Execute_48_Operands:
			case ERigVMOpCode::Execute_49_Operands:
			case ERigVMOpCode::Execute_50_Operands:
			case ERigVMOpCode::Execute_51_Operands:
			case ERigVMOpCode::Execute_52_Operands:
			case ERigVMOpCode::Execute_53_Operands:
			case ERigVMOpCode::Execute_54_Operands:
			case ERigVMOpCode::Execute_55_Operands:
			case ERigVMOpCode::Execute_56_Operands:
			case ERigVMOpCode::Execute_57_Operands:
			case ERigVMOpCode::Execute_58_Operands:
			case ERigVMOpCode::Execute_59_Operands:
			case ERigVMOpCode::Execute_60_Operands:
			case ERigVMOpCode::Execute_61_Operands:
			case ERigVMOpCode::Execute_62_Operands:
			case ERigVMOpCode::Execute_63_Operands:
			case ERigVMOpCode::Execute_64_Operands:
			{
				const FRigVMExecuteOp& Op = ByteCode.GetOpAt<FRigVMExecuteOp>(Instructions[InstructionIndex]);
				int32 OperandCount = FirstPointerForInstruction[InstructionIndex + 1] - FirstPointerForInstruction[InstructionIndex];
				FRigVMOperandMemory OpMemory(&CachedMemoryPointers[FirstPointerForInstruction[InstructionIndex]], OperandCount);
#if WITH_EDITOR
				(*Functions[Op.FunctionIndex])(FunctionNames[Op.FunctionIndex], InstructionIndex, OpMemory, AdditionalArguments);
#else
				(*Functions[Op.FunctionIndex])(NAME_None, INDEX_NONE, OpMemory, AdditionalArguments);
#endif
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Zero:
			{
				const FRigVMUnaryOp& Op = ByteCode.GetOpAt<FRigVMUnaryOp>(Instructions[InstructionIndex]);
				Memory[Op.Arg.GetContainerIndex()]->GetRef<int32>(Op.Arg.GetRegisterIndex()) = 0;
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::BoolFalse:
			{
				*((bool*)CachedMemoryPointers[FirstPointerForInstruction[InstructionIndex]]) = false;
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::BoolTrue:
			{
				*((bool*)CachedMemoryPointers[FirstPointerForInstruction[InstructionIndex]]) = true;
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Copy:
			{
				const FRigVMCopyOp& Op = ByteCode.GetOpAt<FRigVMCopyOp>(Instructions[InstructionIndex]);

				void * SourcePtr = CachedMemoryPointers[FirstPointerForInstruction[InstructionIndex]];
				void * TargetPtr = CachedMemoryPointers[FirstPointerForInstruction[InstructionIndex] + 1];
				uint64 NumBytes = reinterpret_cast<uint64>(CachedMemoryPointers[FirstPointerForInstruction[InstructionIndex] + 2]);
				ERigVMRegisterType MemoryType = (ERigVMRegisterType)reinterpret_cast<uint64>(CachedMemoryPointers[FirstPointerForInstruction[InstructionIndex] + 3]);

				switch (MemoryType)
				{
					case ERigVMRegisterType::Plain:
					{
						FMemory::Memcpy(TargetPtr, SourcePtr, NumBytes);
						break;
					}
					case ERigVMRegisterType::Name:
					{
						int32 NumNames = NumBytes / sizeof(FName);
						TArrayView<FName> TargetNames((FName*)TargetPtr, NumNames);
						TArrayView<FName> SourceNames((FName*)SourcePtr, NumNames);
						for (int32 Index = 0; Index < NumNames; Index++)
						{
							TargetNames[Index] = SourceNames[Index];
						}
						break;
					}
					case ERigVMRegisterType::String:
					{
						int32 NumStrings = NumBytes / sizeof(FString);
						TArrayView<FString> TargetStrings((FString*)TargetPtr, NumStrings);
						TArrayView<FString> SourceStrings((FString*)SourcePtr, NumStrings);
						for (int32 Index = 0; Index < NumStrings; Index++)
						{
							TargetStrings[Index] = SourceStrings[Index];
						}
						break;
					}
					case ERigVMRegisterType::Struct:
					{
						UScriptStruct* ScriptStruct = (UScriptStruct*)CachedMemoryPointers[FirstPointerForInstruction[InstructionIndex] + 4];
						int32 NumStructs = NumBytes / ScriptStruct->GetStructureSize();
						ScriptStruct->CopyScriptStruct(TargetPtr, SourcePtr, NumStructs);
						break;
					}
					default:
					{
						// the default pass for any complex memory
						Memory[Op.Target.GetContainerIndex()]->Copy(Op.Source, Op.Target, Memory[Op.Source.GetContainerIndex()]);
						break;
					}
				}

				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Increment:
			{
				(*(int32*)CachedMemoryPointers[FirstPointerForInstruction[InstructionIndex]])++;
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Decrement:
			{
				(*(int32*)CachedMemoryPointers[FirstPointerForInstruction[InstructionIndex]])--;
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Equals:
			case ERigVMOpCode::NotEquals:
			{
				const FRigVMComparisonOp& Op = ByteCode.GetOpAt<FRigVMComparisonOp>(Instructions[InstructionIndex]);
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
							void * DataA = CachedMemoryPointers[FirstPointerForInstruction[InstructionIndex]];
							void * DataB = CachedMemoryPointers[FirstPointerForInstruction[InstructionIndex]+1];
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

							uint8* DataA = (uint8*)CachedMemoryPointers[FirstPointerForInstruction[InstructionIndex]];
							uint8* DataB = (uint8*)CachedMemoryPointers[FirstPointerForInstruction[InstructionIndex]+1];
							
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
				if (Op.OpCode == ERigVMOpCode::NotEquals)
				{
					Result = !Result;
				}

				(*(bool*)CachedMemoryPointers[FirstPointerForInstruction[InstructionIndex]+2]) = Result;
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
				const bool Condition = *(bool*)CachedMemoryPointers[FirstPointerForInstruction[InstructionIndex]];
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
				const bool Condition = *(bool*)CachedMemoryPointers[FirstPointerForInstruction[InstructionIndex]];
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
				const bool Condition = *(bool*)CachedMemoryPointers[FirstPointerForInstruction[InstructionIndex]];
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

bool URigVM::Execute()
{
	return Execute(FRigVMMemoryContainerPtrArray(), TArrayView<void*>());
}

void URigVM::SetRegisterValueFromString(const FRigVMOperand& InOperand, const FString& InCPPType, const UObject* InCPPTypeObject, const TArray<FString>& InDefaultValues)
{
	if (InOperand.GetMemoryType() == ERigVMMemoryType::Literal)
	{
		LiteralMemory.SetRegisterValueFromString(InOperand, InCPPType, InCPPTypeObject, InDefaultValues);
	}
	else if (InOperand.GetMemoryType() == ERigVMMemoryType::Work)
	{
		WorkMemory.SetRegisterValueFromString(InOperand, InCPPType, InCPPTypeObject, InDefaultValues);
	}
}
