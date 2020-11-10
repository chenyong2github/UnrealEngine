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
    : WorkMemoryPtr(&WorkMemoryStorage)
    , LiteralMemoryPtr(&LiteralMemoryStorage)
    , ByteCodePtr(&ByteCodeStorage)
    , FunctionNamesPtr(&FunctionNamesStorage)
    , FunctionsPtr(&FunctionsStorage)
	, ExecutingThreadId(INDEX_NONE)
	, DeferredVMToCopy(nullptr)
{
	GetWorkMemory().SetMemoryType(ERigVMMemoryType::Work);
	GetLiteralMemory().SetMemoryType(ERigVMMemoryType::Literal);
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

	ensure(ExecutingThreadId == INDEX_NONE);

	if (Ar.IsLoading())
	{
		Reset();
	}

	Ar << WorkMemoryStorage;
	Ar << LiteralMemoryStorage;
	Ar << FunctionNamesStorage;
	Ar << ByteCodeStorage;
	Ar << Parameters;

	if (Ar.IsLoading())
	{
		if (WorkMemoryStorage.bEncounteredErrorDuringLoad ||
			LiteralMemoryStorage.bEncounteredErrorDuringLoad)
		{
			Reset();
		}
		else
		{
			Instructions.Reset();
			FunctionsStorage.Reset();
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
	WorkMemoryStorage.Reset();
	LiteralMemoryStorage.Reset();
	FunctionNamesStorage.Reset();
	FunctionsStorage.Reset();
	ByteCodeStorage.Reset();
	Instructions.Reset();
	Parameters.Reset();
	ParametersNameMap.Reset();
	DeferredVMToCopy = nullptr;

	WorkMemoryPtr = &WorkMemoryStorage;
	LiteralMemoryPtr = &LiteralMemoryStorage;
	FunctionNamesPtr = &FunctionNamesStorage;
	FunctionsPtr = &FunctionsStorage;
	ByteCodePtr = &ByteCodeStorage;

	InvalidateCachedMemory();
}

void URigVM::Empty()
{
	WorkMemoryStorage.Empty();
	LiteralMemoryStorage.Empty();
	FunctionNamesStorage.Empty();
	FunctionsStorage.Empty();
	ByteCodeStorage.Empty();
	Instructions.Empty();
	Parameters.Empty();
	ParametersNameMap.Empty();
	DeferredVMToCopy = nullptr;
	ExternalVariables.Empty();

	InvalidateCachedMemory();

	CachedMemory.Empty();
	FirstHandleForInstruction.Empty();
	CachedMemoryHandles.Empty();
}

void URigVM::CopyFrom(URigVM* InVM, bool bDeferCopy, bool bReferenceLiteralMemory, bool bReferenceByteCode)
{
	check(InVM);

	// if this vm is currently executing on a worker thread
	// we defer the copy until the next execute
	if (ExecutingThreadId != INDEX_NONE || bDeferCopy)
	{
		DeferredVMToCopy = InVM;
		return;
	}
	
	Reset();

	if(InVM->WorkMemoryPtr == &InVM->WorkMemoryStorage)
	{
		WorkMemoryStorage = InVM->WorkMemoryStorage;
		WorkMemoryPtr = &WorkMemoryStorage;
	}
	else
	{
		WorkMemoryPtr = InVM->WorkMemoryPtr;
	}

	if(InVM->LiteralMemoryPtr == &InVM->LiteralMemoryStorage && !bReferenceLiteralMemory)
	{
		LiteralMemoryStorage = InVM->LiteralMemoryStorage;
		LiteralMemoryPtr = &LiteralMemoryStorage;
	}
	else
	{
		LiteralMemoryPtr = InVM->LiteralMemoryPtr;
	}

	if(InVM->FunctionNamesPtr == &InVM->FunctionNamesStorage && !bReferenceByteCode)
	{
		FunctionNamesStorage = InVM->FunctionNamesStorage;
		FunctionNamesPtr = &FunctionNamesStorage;
	}
	else
	{
		FunctionNamesPtr = InVM->FunctionNamesPtr;
	}
	
	if(InVM->FunctionsPtr == &InVM->FunctionsStorage && !bReferenceByteCode)
	{
		FunctionsStorage = InVM->FunctionsStorage;
		FunctionsPtr = &FunctionsStorage;
	}
	else
	{
		FunctionsPtr = InVM->FunctionsPtr;
	}
	
	if(InVM->ByteCodePtr == &InVM->ByteCodeStorage && !bReferenceByteCode)
	{
		ByteCodeStorage = InVM->ByteCodeStorage;
		ByteCodePtr = &ByteCodeStorage;
		ByteCodePtr->bByteCodeIsAligned = InVM->ByteCodeStorage.bByteCodeIsAligned;
	}
	else
	{
		ByteCodePtr = InVM->ByteCodePtr;
	}
	
	Instructions = InVM->Instructions;
	Parameters = InVM->Parameters;
	ParametersNameMap = InVM->ParametersNameMap;
}

int32 URigVM::AddRigVMFunction(UScriptStruct* InRigVMStruct, const FName& InMethodName)
{
	check(InRigVMStruct);
	FString FunctionKey = FString::Printf(TEXT("F%s::%s"), *InRigVMStruct->GetName(), *InMethodName.ToString());
	int32 FunctionIndex = GetFunctionNames().Find(*FunctionKey);
	if (FunctionIndex != INDEX_NONE)
	{
		return FunctionIndex;
	}

	FRigVMFunctionPtr Function = FRigVMRegistry::Get().FindFunction(*FunctionKey);
	if (Function == nullptr)
	{
		return INDEX_NONE;
	}

	GetFunctionNames().Add(*FunctionKey);
	return GetFunctions().Add(Function);
}

FString URigVM::GetRigVMFunctionName(int32 InFunctionIndex) const
{
	return GetFunctionNames()[InFunctionIndex].ToString();
}

const FRigVMInstructionArray& URigVM::GetInstructions()
{
	RefreshInstructionsIfRequired();
	return Instructions;
}

bool URigVM::ContainsEntry(const FName& InEntryName) const
{
	const FRigVMByteCode& ByteCode = GetByteCode();
	return ByteCode.FindEntryIndex(InEntryName) != INDEX_NONE;
}

TArray<FName> URigVM::GetEntryNames() const
{
	TArray<FName> EntryNames;

	const FRigVMByteCode& ByteCode = GetByteCode();
	for (int32 EntryIndex = 0; EntryIndex < ByteCode.NumEntries(); EntryIndex++)
	{
		EntryNames.Add(ByteCode.GetEntry(EntryIndex).Name);
	}

	return EntryNames;
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
	if (GetFunctions().Num() != GetFunctionNames().Num())
	{
		GetFunctions().Reset();
		GetFunctions().SetNumZeroed(GetFunctionNames().Num());

		for (int32 FunctionIndex = 0; FunctionIndex < GetFunctionNames().Num(); FunctionIndex++)
		{
			GetFunctions()[FunctionIndex] = FRigVMRegistry::Get().FindFunction(*GetFunctionNames()[FunctionIndex].ToString());
		}
	}
}

void URigVM::RefreshInstructionsIfRequired()
{
	if (GetByteCode().Num() == 0 && Instructions.Num() > 0)
	{
		Instructions.Reset();
	}
	else if (Instructions.Num() == 0)
	{
		Instructions = GetByteCode().GetInstructions();
	}
}

void URigVM::InvalidateCachedMemory()
{
	CachedMemory.Reset();
	FirstHandleForInstruction.Reset();
	CachedMemoryHandles.Reset();
}

void URigVM::CopyDeferredVMIfRequired()
{
	ensure(ExecutingThreadId == INDEX_NONE);

	URigVM* VMToCopy = nullptr;
	Swap(VMToCopy, DeferredVMToCopy);

	if (VMToCopy)
	{
		CopyFrom(VMToCopy);
	}
}

void URigVM::CacheMemoryHandlesIfRequired(FRigVMMemoryContainerPtrArray InMemory)
{
	ensureMsgf(ExecutingThreadId == FPlatformTLS::GetCurrentThreadId(), TEXT("RigVM::CacheMemoryHandlesIfRequired from multiple threads (%d and %d)"), ExecutingThreadId, (int32)FPlatformTLS::GetCurrentThreadId());

	RefreshInstructionsIfRequired();

	if (Instructions.Num() == 0 || InMemory.Num() == 0)
	{
		InvalidateCachedMemory();
		return;
	}

	if (Instructions.Num() != FirstHandleForInstruction.Num())
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

	if (Instructions.Num() == FirstHandleForInstruction.Num())
	{
		return;
	}

	for (int32 Index = 0; Index < InMemory.Num(); Index++)
	{
		CachedMemory.Add(InMemory[Index]);
	}

	FRigVMByteCode& ByteCode = GetByteCode();

	uint16 InstructionIndex = 0;
	while (Instructions.IsValidIndex(InstructionIndex))
	{
		FirstHandleForInstruction.Add(CachedMemoryHandles.Num());

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
					CacheSingleMemoryHandle(Arg, true);
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
				CacheSingleMemoryHandle(Op.Arg);
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Copy:
			{
				const FRigVMCopyOp& Op = ByteCode.GetOpAt<FRigVMCopyOp>(Instructions[InstructionIndex]);
				CacheSingleMemoryHandle(Op.Source);
				CacheSingleMemoryHandle(Op.Target);

				uint16 NumBytes = 0;
				ERigVMRegisterType TargetType = ERigVMRegisterType::Invalid;
				UScriptStruct* ScriptStruct = nullptr;

				if (Op.Target.GetMemoryType() == ERigVMMemoryType::External)
				{
					ensure(ExternalVariables.IsValidIndex(Op.Target.GetRegisterIndex()));
					const FRigVMExternalVariable& ExternalVariable = ExternalVariables[Op.Target.GetRegisterIndex()];

					NumBytes = ExternalVariable.Size;
					TargetType = ERigVMRegisterType::Plain;
					if (UScriptStruct* ExternalScriptStruct = Cast<UScriptStruct>(ExternalVariable.TypeObject))	
					{
						TargetType = ERigVMRegisterType::Struct;
						ScriptStruct = ExternalScriptStruct;
					}
					else if (ExternalVariable.TypeName == TEXT("FString"))
					{
						TargetType = ERigVMRegisterType::String;
					}
					else if (ExternalVariable.TypeName == TEXT("FName"))
					{
						TargetType = ERigVMRegisterType::Name;
					}
				}
				else
				{
					const FRigVMRegister& TargetRegister = CachedMemory[Op.Target.GetContainerIndex()]->Registers[Op.Target.GetRegisterIndex()];
					NumBytes = TargetRegister.GetNumBytesPerSlice();

					TargetType = TargetRegister.Type;

					if (Op.Target.GetRegisterOffset() == INDEX_NONE)
					{
						if (TargetRegister.IsArray())
						{
							const FRigVMRegister& SourceRegister = CachedMemory[Op.Source.GetContainerIndex()]->Registers[Op.Source.GetRegisterIndex()];
							if (!SourceRegister.IsArray())
							{
								if (Op.Source.GetRegisterOffset() == INDEX_NONE)
								{
									NumBytes = TargetRegister.ElementSize;
								}
								else
								{
									const FRigVMRegisterOffset& SourceOffset = CachedMemory[Op.Source.GetContainerIndex()]->RegisterOffsets[Op.Source.GetRegisterOffset()];
									if (SourceOffset.GetCPPType() != TEXT("TArray"))
									{
										NumBytes = SourceOffset.GetElementSize();
									}
								}
							}
						}
					}
					else
					{
						TargetType = CachedMemory[Op.Target.GetContainerIndex()]->RegisterOffsets[Op.Target.GetRegisterOffset()].GetType();
						NumBytes = CachedMemory[Op.Target.GetContainerIndex()]->RegisterOffsets[Op.Target.GetRegisterOffset()].GetElementSize();
					}

					if (TargetType == ERigVMRegisterType::Struct)
					{
						ScriptStruct = CachedMemory[Op.Target.GetContainerIndex()]->GetScriptStruct(Op.Target.GetRegisterIndex(), Op.Target.GetRegisterOffset());
					}
				}

				CachedMemoryHandles.Add(FRigVMMemoryHandle((uint8*)reinterpret_cast<void*>(NumBytes)));
				CachedMemoryHandles.Add(FRigVMMemoryHandle((uint8*)reinterpret_cast<void*>((uint16)TargetType)));

				if (TargetType == ERigVMRegisterType::Struct)
				{
					CachedMemoryHandles.Add(FRigVMMemoryHandle((uint8*)ScriptStruct));
				}

				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Equals:
			case ERigVMOpCode::NotEquals:
			{
				const FRigVMComparisonOp& Op = ByteCode.GetOpAt<FRigVMComparisonOp>(Instructions[InstructionIndex]);
				FRigVMOperand Arg = Op.A;
				CacheSingleMemoryHandle(Arg);
				Arg = Op.B;
				CacheSingleMemoryHandle(Arg);
				Arg = Op.Result;
				CacheSingleMemoryHandle(Arg);
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
				CacheSingleMemoryHandle(Arg);
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::ChangeType:
			{
				const FRigVMChangeTypeOp& Op = ByteCode.GetOpAt<FRigVMChangeTypeOp>(Instructions[InstructionIndex]);
				const FRigVMOperand& Arg = Op.Arg;
				CacheSingleMemoryHandle(Arg);
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Exit:
			{
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::BeginBlock:
			{
				const FRigVMBinaryOp& Op = ByteCode.GetOpAt<FRigVMBinaryOp>(Instructions[InstructionIndex]);
				CacheSingleMemoryHandle(Op.ArgA);
				CacheSingleMemoryHandle(Op.ArgB);
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::EndBlock:
			{
				InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Invalid:
			{
				ensure(false);
				break;
			}
		}
	}

	if (FirstHandleForInstruction.Num() < Instructions.Num())
	{
		FirstHandleForInstruction.Add(CachedMemoryHandles.Num());
	}
}

bool URigVM::Initialize(FRigVMMemoryContainerPtrArray Memory, FRigVMFixedArray<void*> AdditionalArguments)
{
	if (ExecutingThreadId != INDEX_NONE)
	{
		ensureMsgf(ExecutingThreadId == FPlatformTLS::GetCurrentThreadId(), TEXT("RigVM::Initialize from multiple threads (%d and %d)"), ExecutingThreadId, (int32)FPlatformTLS::GetCurrentThreadId());
	}
	CopyDeferredVMIfRequired();
	TGuardValue<int32> GuardThreadId(ExecutingThreadId, FPlatformTLS::GetCurrentThreadId());

	ResolveFunctionsIfRequired();
	RefreshInstructionsIfRequired();

	if (Instructions.Num() == 0)
	{
		return true;
	}

	FRigVMMemoryContainer* LocalMemory[] = { WorkMemoryPtr, LiteralMemoryPtr };
	if (Memory.Num() == 0)
	{
		Memory = FRigVMMemoryContainerPtrArray(LocalMemory, 2);
	}

	CacheMemoryHandlesIfRequired(Memory);
	FRigVMByteCode& ByteCode = GetByteCode();
	TArray<FRigVMFunctionPtr>& Functions = GetFunctions();

#if WITH_EDITOR
	TArray<FName>& FunctionNames = GetFunctionNames();
#endif

	Context.Reset();
	Context.SliceOffsets.AddZeroed(Instructions.Num());
	Context.OpaqueArguments = AdditionalArguments;
	Context.ExternalVariables = ExternalVariables;

	while (Instructions.IsValidIndex(Context.InstructionIndex))
	{
		const FRigVMInstruction& Instruction = Instructions[Context.InstructionIndex];

		switch (Instruction.OpCode)
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
				const FRigVMExecuteOp& Op = ByteCode.GetOpAt<FRigVMExecuteOp>(Instruction);
				int32 OperandCount = FirstHandleForInstruction[Context.InstructionIndex + 1] - FirstHandleForInstruction[Context.InstructionIndex];
				FRigVMMemoryHandleArray OpHandles(&CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]], OperandCount);
#if WITH_EDITOR
				Context.FunctionName = FunctionNames[Op.FunctionIndex];
#endif

				// find out the largest slice count
				int32 MaxSliceCount = 1;
				for (const FRigVMMemoryHandle& OpHandle : OpHandles)
				{
					if (OpHandle.Type == FRigVMMemoryHandle::Dynamic)
					{
						if (const FRigVMByteArray* Storage = (const FRigVMByteArray*)OpHandle.Ptr)
						{
							MaxSliceCount = FMath::Max<int32>(MaxSliceCount, Storage->Num() / OpHandle.Size);
						}
					}
					else if (OpHandle.Type == FRigVMMemoryHandle::NestedDynamic)
					{
						if (const FRigVMNestedByteArray* Storage = (const FRigVMNestedByteArray*)OpHandle.Ptr)
						{
							MaxSliceCount = FMath::Max<int32>(MaxSliceCount, Storage->Num());
						}
					}
				}

				Context.BeginSlice(MaxSliceCount);
				for (int32 SliceIndex = 0; SliceIndex < MaxSliceCount; SliceIndex++)
				{
					(*Functions[Op.FunctionIndex])(Context, OpHandles);
					Context.IncrementSlice();
				}
				Context.EndSlice();

				break;
			}
			case ERigVMOpCode::Zero:
			case ERigVMOpCode::BoolFalse:
			case ERigVMOpCode::BoolTrue:
			{
				break;
			}
			case ERigVMOpCode::Copy:
			{
				const FRigVMCopyOp& Op = ByteCode.GetOpAt<FRigVMCopyOp>(Instruction);

				FRigVMMemoryHandle& SourceHandle = CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]];
				FRigVMMemoryHandle& TargetHandle = CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex] + 1];
				void* SourcePtr = SourceHandle;
				void* TargetPtr = TargetHandle;

				uint64 NumBytes = reinterpret_cast<uint64>(CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex] + 2].GetData());
				ERigVMRegisterType MemoryType = (ERigVMRegisterType)reinterpret_cast<uint64>(CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex] + 3].GetData());

				if (TargetHandle.Type == FRigVMMemoryHandle::Dynamic)
				{
					FRigVMByteArray* Storage = (FRigVMByteArray*)TargetHandle.Ptr;
					if (Context.GetSlice().GetIndex() == 0)
					{
						Storage->Reset();
					}
					int32 ByteIndex = Storage->AddZeroed(NumBytes);
					TargetPtr = Storage->GetData() + ByteIndex;
				}
				else if (TargetHandle.Type == FRigVMMemoryHandle::NestedDynamic)
				{
					FRigVMNestedByteArray* Storage = (FRigVMNestedByteArray*)TargetHandle.Ptr;
					if (Context.GetSlice().GetIndex() == 0)
					{
						Storage->Reset();
					}
					int32 ArrayIndex = Storage->Add(FRigVMByteArray());
					(*Storage)[ArrayIndex].AddZeroed(NumBytes);
					TargetPtr = (*Storage)[ArrayIndex].GetData();
				}

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
						FRigVMFixedArray<FName> TargetNames((FName*)TargetPtr, NumNames);
						FRigVMFixedArray<FName> SourceNames((FName*)SourcePtr, NumNames);
						for (int32 Index = 0; Index < NumNames; Index++)
						{
							TargetNames[Index] = SourceNames[Index];
						}
						break;
					}
					case ERigVMRegisterType::String:
					{
						int32 NumStrings = NumBytes / sizeof(FString);
						FRigVMFixedArray<FString> TargetStrings((FString*)TargetPtr, NumStrings);
						FRigVMFixedArray<FString> SourceStrings((FString*)SourcePtr, NumStrings);
						for (int32 Index = 0; Index < NumStrings; Index++)
						{
							TargetStrings[Index] = SourceStrings[Index];
						}
						break;
					}
					case ERigVMRegisterType::Struct:
					{
						UScriptStruct* ScriptStruct = (UScriptStruct*)CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex] + 4].GetData();
						int32 NumStructs = NumBytes / ScriptStruct->GetStructureSize();
						if (NumStructs > 0 && TargetPtr)
						{
							ScriptStruct->CopyScriptStruct(TargetPtr, SourcePtr, NumStructs);
						}
						break;
					}
					default:
					{
						// the default pass for any complex memory
						Memory[Op.Target.GetContainerIndex()]->Copy(Op.Source, Op.Target, Memory[Op.Source.GetContainerIndex()]);
						break;
					}
				}
				break;
			}
			case ERigVMOpCode::Increment:
			case ERigVMOpCode::Decrement:
			case ERigVMOpCode::Equals:
			case ERigVMOpCode::NotEquals:
			case ERigVMOpCode::JumpAbsolute:
			case ERigVMOpCode::JumpForward:
			case ERigVMOpCode::JumpBackward:
			case ERigVMOpCode::JumpAbsoluteIf:
			case ERigVMOpCode::JumpForwardIf:
			case ERigVMOpCode::JumpBackwardIf:
			case ERigVMOpCode::ChangeType:
			case ERigVMOpCode::BeginBlock:
			case ERigVMOpCode::EndBlock:
			case ERigVMOpCode::Exit:
			{
				break;
			}
			case ERigVMOpCode::Invalid:
			{
				ensure(false);
				return false;
			}
		}
		Context.InstructionIndex++;
	}

	return true;
}

bool URigVM::Execute(FRigVMMemoryContainerPtrArray Memory, FRigVMFixedArray<void*> AdditionalArguments, const FName& InEntryName)
{
	if (ExecutingThreadId != INDEX_NONE)
	{
		ensureMsgf(ExecutingThreadId == FPlatformTLS::GetCurrentThreadId(), TEXT("RigVM::Execute from multiple threads (%d and %d)"), ExecutingThreadId, (int32)FPlatformTLS::GetCurrentThreadId());
	}
	CopyDeferredVMIfRequired();
	TGuardValue<int32> GuardThreadId(ExecutingThreadId, FPlatformTLS::GetCurrentThreadId());

	ResolveFunctionsIfRequired();
	RefreshInstructionsIfRequired();

	if (Instructions.Num() == 0)
	{
		return true;
	}

	FRigVMMemoryContainer* LocalMemory[] = { WorkMemoryPtr, LiteralMemoryPtr }; 
	if (Memory.Num() == 0)
	{
		Memory = FRigVMMemoryContainerPtrArray(LocalMemory, 2);
	}

	CacheMemoryHandlesIfRequired(Memory);
	FRigVMByteCode& ByteCode = GetByteCode();
	TArray<FRigVMFunctionPtr>& Functions = GetFunctions();

#if WITH_EDITOR
	TArray<FName>& FunctionNames = GetFunctionNames();
	InstructionVisitedDuringLastRun.Reset();
	InstructionVisitOrder.Reset();
	InstructionVisitedDuringLastRun.SetNumZeroed(Instructions.Num());
#endif

	Context.Reset();
	Context.SliceOffsets.AddZeroed(Instructions.Num());
	Context.OpaqueArguments = AdditionalArguments;
	Context.ExternalVariables = ExternalVariables;

	if (!InEntryName.IsNone())
	{
		int32 EntryIndex = ByteCode.FindEntryIndex(InEntryName);
		if (EntryIndex == INDEX_NONE)
		{
			return false;
		}
		Context.InstructionIndex = (uint16)ByteCode.GetEntry(EntryIndex).InstructionIndex;
	}

	while (Instructions.IsValidIndex(Context.InstructionIndex))
	{
#if WITH_EDITOR
		InstructionVisitedDuringLastRun[Context.InstructionIndex] = true;
		InstructionVisitOrder.Add(Context.InstructionIndex);
#endif

		const FRigVMInstruction& Instruction = Instructions[Context.InstructionIndex];

		switch (Instruction.OpCode)
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
				const FRigVMExecuteOp& Op = ByteCode.GetOpAt<FRigVMExecuteOp>(Instruction);
				int32 OperandCount = FirstHandleForInstruction[Context.InstructionIndex + 1] - FirstHandleForInstruction[Context.InstructionIndex];
				FRigVMMemoryHandleArray OpHandles(&CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]], OperandCount);
#if WITH_EDITOR
				Context.FunctionName = FunctionNames[Op.FunctionIndex];
#endif
				(*Functions[Op.FunctionIndex])(Context, OpHandles);
				Context.InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Zero:
			{
				*((int32*)CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]].GetData()) = 0;
				Context.InstructionIndex++;
				break;
			}
			case ERigVMOpCode::BoolFalse:
			{
				*((bool*)CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]].GetData()) = false;
				Context.InstructionIndex++;
				break;
			}
			case ERigVMOpCode::BoolTrue:
			{
				*((bool*)CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]].GetData()) = true;
				Context.InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Copy:
			{
				const FRigVMCopyOp& Op = ByteCode.GetOpAt<FRigVMCopyOp>(Instruction);

				FRigVMMemoryHandle& SourceHandle = CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]];
				FRigVMMemoryHandle& TargetHandle = CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex] + 1];
				void* SourcePtr = SourceHandle;
				void* TargetPtr = TargetHandle;

				uint64 NumBytes = reinterpret_cast<uint64>(CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex] + 2].GetData());
				ERigVMRegisterType MemoryType = (ERigVMRegisterType)reinterpret_cast<uint64>(CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex] + 3].GetData());

				if (TargetHandle.Type == FRigVMMemoryHandle::Dynamic)
				{
					FRigVMByteArray* Storage = (FRigVMByteArray*)TargetHandle.Ptr;
					if (Context.GetSlice().GetIndex() == 0)
					{
						Storage->Reset();
					}
					int32 ByteIndex = Storage->AddZeroed(NumBytes);
					TargetPtr = Storage->GetData() + ByteIndex;
				}
				else if (TargetHandle.Type == FRigVMMemoryHandle::NestedDynamic)
				{
					FRigVMNestedByteArray* Storage = (FRigVMNestedByteArray*)TargetHandle.Ptr;
					if (Context.GetSlice().GetIndex() == 0)
					{
						Storage->Reset();
					}
					int32 ArrayIndex = Storage->Add(FRigVMByteArray());
					(*Storage)[ArrayIndex].AddZeroed(NumBytes);
					TargetPtr = (*Storage)[ArrayIndex].GetData();
				}

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
						FRigVMFixedArray<FName> TargetNames((FName*)TargetPtr, NumNames);
						FRigVMFixedArray<FName> SourceNames((FName*)SourcePtr, NumNames);
						for (int32 Index = 0; Index < NumNames; Index++)
						{
							TargetNames[Index] = SourceNames[Index];
						}
						break;
					}
					case ERigVMRegisterType::String:
					{
						int32 NumStrings = NumBytes / sizeof(FString);
						FRigVMFixedArray<FString> TargetStrings((FString*)TargetPtr, NumStrings);
						FRigVMFixedArray<FString> SourceStrings((FString*)SourcePtr, NumStrings);
						for (int32 Index = 0; Index < NumStrings; Index++)
						{
							TargetStrings[Index] = SourceStrings[Index];
						}
						break;
					}
					case ERigVMRegisterType::Struct:
					{
						UScriptStruct* ScriptStruct = (UScriptStruct*)CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex] + 4].GetData();
						int32 NumStructs = NumBytes / ScriptStruct->GetStructureSize();
						if (NumStructs > 0 && TargetPtr)
						{
							ScriptStruct->CopyScriptStruct(TargetPtr, SourcePtr, NumStructs);
						}
						break;
					}
					default:
					{
						// the default pass for any complex memory
						Memory[Op.Target.GetContainerIndex()]->Copy(Op.Source, Op.Target, Memory[Op.Source.GetContainerIndex()]);
						break;
					}
				}

				Context.InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Increment:
			{
				(*((int32*)CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]].GetData()))++;
				Context.InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Decrement:
			{
				(*((int32*)CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]].GetData()))--;
				Context.InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Equals:
			case ERigVMOpCode::NotEquals:
			{
				const FRigVMComparisonOp& Op = ByteCode.GetOpAt<FRigVMComparisonOp>(Instruction);
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
							void* DataA = CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]].GetData();
							void* DataB = CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]+1].GetData();
							Result = FMemory::Memcmp(DataA, DataB, BytesA) == 0;
							break;
						}
						case ERigVMRegisterType::String:
						{
							FRigVMFixedArray<FString> StringsA = Memory[Op.A.GetContainerIndex()]->GetFixedArray<FString>(Op.A.GetRegisterIndex());
							FRigVMFixedArray<FString> StringsB = Memory[Op.B.GetContainerIndex()]->GetFixedArray<FString>(Op.B.GetRegisterIndex());

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

							uint8* DataA = (uint8*)CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]].GetData();
							uint8* DataB = (uint8*)CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]+1].GetData();
							
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

				*((bool*)CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]+2].GetData()) = Result;
				Context.InstructionIndex++;
				break;
			}
			case ERigVMOpCode::JumpAbsolute:
			{
				const FRigVMJumpOp& Op = ByteCode.GetOpAt<FRigVMJumpOp>(Instruction);
				Context.InstructionIndex = Op.InstructionIndex;
				break;
			}
			case ERigVMOpCode::JumpForward:
			{
				const FRigVMJumpOp& Op = ByteCode.GetOpAt<FRigVMJumpOp>(Instruction);
				Context.InstructionIndex += Op.InstructionIndex;
				break;
			}
			case ERigVMOpCode::JumpBackward:
			{
				const FRigVMJumpOp& Op = ByteCode.GetOpAt<FRigVMJumpOp>(Instruction);
				Context.InstructionIndex -= Op.InstructionIndex;
				break;
			}
			case ERigVMOpCode::JumpAbsoluteIf:
			{
				const FRigVMJumpIfOp& Op = ByteCode.GetOpAt<FRigVMJumpIfOp>(Instruction);
				bool Condition = *(bool*)CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]].GetData();
				if (Condition == Op.Condition)
				{
					Context.InstructionIndex = Op.InstructionIndex;
				}
				else
				{
					Context.InstructionIndex++;
				}
				break;
			}
			case ERigVMOpCode::JumpForwardIf:
			{
				const FRigVMJumpIfOp& Op = ByteCode.GetOpAt<FRigVMJumpIfOp>(Instruction);
				bool Condition = *(bool*)CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]].GetData();
				if (Condition == Op.Condition)
				{
					Context.InstructionIndex += Op.InstructionIndex;
				}
				else
				{
					Context.InstructionIndex++;
				}
				break;
			}
			case ERigVMOpCode::JumpBackwardIf:
			{
				const FRigVMJumpIfOp& Op = ByteCode.GetOpAt<FRigVMJumpIfOp>(Instruction);
				bool Condition = *(bool*)CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]].GetData();
				if (Condition == Op.Condition)
				{
					Context.InstructionIndex -= Op.InstructionIndex;
				}
				else
				{
					Context.InstructionIndex++;
				}
				break;
			}
			case ERigVMOpCode::ChangeType:
			{
				ensureMsgf(false, TEXT("not implemented."));
				break;
			}
			case ERigVMOpCode::Exit:
			{
				return true;
			}
			case ERigVMOpCode::BeginBlock:
			{
				int32 Count = (*((int32*)CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]].GetData()));
				int32 Index = (*((int32*)CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex] + 1].GetData()));
				Context.BeginSlice(Count, Index);
				Context.InstructionIndex++;
				break;
			}
			case ERigVMOpCode::EndBlock:
			{
				Context.EndSlice();
				Context.InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Invalid:
			{
				ensure(false);
				return false;
			}
		}
	}

	return true;
}

bool URigVM::Execute(const FName& InEntryName)
{
	return Execute(FRigVMMemoryContainerPtrArray(), FRigVMFixedArray<void*>(), InEntryName);
}

FRigVMExternalVariable URigVM::GetExternalVariableByName(const FName& InExternalVariableName)
{
	for (const FRigVMExternalVariable& ExternalVariable : ExternalVariables)
	{
		if (ExternalVariable.Name == InExternalVariableName)
		{
			return ExternalVariable;
		}
	}
	return FRigVMExternalVariable();
}

void URigVM::SetRegisterValueFromString(const FRigVMOperand& InOperand, const FString& InCPPType, const UObject* InCPPTypeObject, const TArray<FString>& InDefaultValues)
{
	if (InOperand.GetMemoryType() == ERigVMMemoryType::Literal)
	{
		GetLiteralMemory().SetRegisterValueFromString(InOperand, InCPPType, InCPPTypeObject, InDefaultValues);
	}
	else if (InOperand.GetMemoryType() == ERigVMMemoryType::Work)
	{
		GetWorkMemory().SetRegisterValueFromString(InOperand, InCPPType, InCPPTypeObject, InDefaultValues);
	}
}


#if WITH_EDITOR

TArray<FString> URigVM::DumpByteCodeAsTextArray(const TArray<int32>& InInstructionOrder, bool bIncludeLineNumbers)
{
	RefreshInstructionsIfRequired();
	const FRigVMByteCode& ByteCode = GetByteCode();
	const TArray<FName>& FunctionNames = GetFunctionNames();

	TArray<int32> InstructionOrder;
	InstructionOrder.Append(InInstructionOrder);
	if (InstructionOrder.Num() == 0)
	{
		for (int32 InstructionIndex = 0; InstructionIndex < Instructions.Num(); InstructionIndex++)
		{
			InstructionOrder.Add(InstructionIndex);
		}
	}

	TArray<FString> Result;

	for (int32 InstructionIndex : InstructionOrder)
	{
		FString ResultLine;

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
				FString FunctionName = FunctionNames[Op.FunctionIndex].ToString();
				FRigVMOperandArray Operands = ByteCode.GetOperandsForExecuteOp(Instructions[InstructionIndex]);

				TArray<FString> Labels;
				for (const FRigVMOperand& Operand : Operands)
				{
					Labels.Add(GetOperandLabel(Operand));
				}

				ResultLine = FString::Printf(TEXT("%s(%s)"), *FunctionName, *FString::Join(Labels, TEXT(",")));
				break;
			}
			case ERigVMOpCode::Zero:
			{
				const FRigVMUnaryOp& Op = ByteCode.GetOpAt<FRigVMUnaryOp>(Instructions[InstructionIndex]);
				ResultLine = FString::Printf(TEXT("Set %s to 0"), *GetOperandLabel(Op.Arg));
				break;
			}
			case ERigVMOpCode::BoolFalse:
			{
				const FRigVMUnaryOp& Op = ByteCode.GetOpAt<FRigVMUnaryOp>(Instructions[InstructionIndex]);
				ResultLine = FString::Printf(TEXT("Set %s to False"), *GetOperandLabel(Op.Arg));
				break;
			}
			case ERigVMOpCode::BoolTrue:
			{
				const FRigVMUnaryOp& Op = ByteCode.GetOpAt<FRigVMUnaryOp>(Instructions[InstructionIndex]);
				ResultLine = FString::Printf(TEXT("Set %s to True"), *GetOperandLabel(Op.Arg));
				break;
			}
			case ERigVMOpCode::Increment:
			{
				const FRigVMUnaryOp& Op = ByteCode.GetOpAt<FRigVMUnaryOp>(Instructions[InstructionIndex]);
				ResultLine = FString::Printf(TEXT("Inc %s ++"), *GetOperandLabel(Op.Arg));
				break;
			}
			case ERigVMOpCode::Decrement:
			{
				const FRigVMUnaryOp& Op = ByteCode.GetOpAt<FRigVMUnaryOp>(Instructions[InstructionIndex]);
				ResultLine = FString::Printf(TEXT("Dec %s --"), *GetOperandLabel(Op.Arg));
				break;
			}
			case ERigVMOpCode::Copy:
			{
				const FRigVMCopyOp& Op = ByteCode.GetOpAt<FRigVMCopyOp>(Instructions[InstructionIndex]);
				ResultLine = FString::Printf(TEXT("Copy %s to %s"), *GetOperandLabel(Op.Source), *GetOperandLabel(Op.Target));
				break;
			}
			case ERigVMOpCode::Equals:
			{
				const FRigVMComparisonOp& Op = ByteCode.GetOpAt<FRigVMComparisonOp>(Instructions[InstructionIndex]);
				ResultLine = FString::Printf(TEXT("Set %s to %s == %s "), *GetOperandLabel(Op.Result), *GetOperandLabel(Op.A), *GetOperandLabel(Op.B));
				break;
			}
			case ERigVMOpCode::NotEquals:
			{
				const FRigVMComparisonOp& Op = ByteCode.GetOpAt<FRigVMComparisonOp>(Instructions[InstructionIndex]);
				ResultLine = FString::Printf(TEXT("Set %s to %s != %s"), *GetOperandLabel(Op.Result), *GetOperandLabel(Op.A), *GetOperandLabel(Op.B));
				break;
			}
			case ERigVMOpCode::JumpAbsolute:
			{
				const FRigVMJumpOp& Op = ByteCode.GetOpAt<FRigVMJumpOp>(Instructions[InstructionIndex]);
				ResultLine = FString::Printf(TEXT("Jump to instruction %d"), Op.InstructionIndex);
				break;
			}
			case ERigVMOpCode::JumpForward:
			{
				const FRigVMJumpOp& Op = ByteCode.GetOpAt<FRigVMJumpOp>(Instructions[InstructionIndex]);
				ResultLine = FString::Printf(TEXT("Jump %d instructions forwards"), Op.InstructionIndex);
				break;
			}
			case ERigVMOpCode::JumpBackward:
			{
				const FRigVMJumpOp& Op = ByteCode.GetOpAt<FRigVMJumpOp>(Instructions[InstructionIndex]);
				ResultLine = FString::Printf(TEXT("Jump %d instructions backwards"), Op.InstructionIndex);
				break;
			}
			case ERigVMOpCode::JumpAbsoluteIf:
			{
				const FRigVMJumpIfOp& Op = ByteCode.GetOpAt<FRigVMJumpIfOp>(Instructions[InstructionIndex]);
				if (Op.Condition)
				{
					ResultLine = FString::Printf(TEXT("Jump to instruction %d if %s"), Op.InstructionIndex, *GetOperandLabel(Op.Arg));
				}
				else
				{
					ResultLine = FString::Printf(TEXT("Jump to instruction %d if !%s"), Op.InstructionIndex, *GetOperandLabel(Op.Arg));
				}
				break;
			}
			case ERigVMOpCode::JumpForwardIf:
			{
				const FRigVMJumpIfOp& Op = ByteCode.GetOpAt<FRigVMJumpIfOp>(Instructions[InstructionIndex]);
				if (Op.Condition)
				{
					ResultLine = FString::Printf(TEXT("Jump %d instructions forwards if %s"), Op.InstructionIndex, *GetOperandLabel(Op.Arg));
				}
				else
				{
					ResultLine = FString::Printf(TEXT("Jump %d instructions forwards if !%s"), Op.InstructionIndex, *GetOperandLabel(Op.Arg));
				}
				break;
			}
			case ERigVMOpCode::JumpBackwardIf:
			{
				const FRigVMJumpIfOp& Op = ByteCode.GetOpAt<FRigVMJumpIfOp>(Instructions[InstructionIndex]);
				if (Op.Condition)
				{
					ResultLine = FString::Printf(TEXT("Jump %d instructions backwards if %s"), Op.InstructionIndex, *GetOperandLabel(Op.Arg));
				}
				else
				{
					ResultLine = FString::Printf(TEXT("Jump %d instructions backwards if !%s"), Op.InstructionIndex, *GetOperandLabel(Op.Arg));
				}
				break;
			}
			case ERigVMOpCode::ChangeType:
			{
				const FRigVMChangeTypeOp& Op = ByteCode.GetOpAt<FRigVMChangeTypeOp>(Instructions[InstructionIndex]);
				ResultLine = FString::Printf(TEXT("Change type of %s"), *GetOperandLabel(Op.Arg));
				break;
			}
			case ERigVMOpCode::Exit:
			{
				ResultLine = TEXT("Exit");
				break;
			}
			case ERigVMOpCode::BeginBlock:
			{
				ResultLine = TEXT("Begin Block");
				break;
			}
			case ERigVMOpCode::EndBlock:
			{
				ResultLine = TEXT("End Block");
				break;
			}
			default:
			{
				ensure(false);
				break;
			}
		}

		if (bIncludeLineNumbers)
		{
			FString ResultIndexStr = FString::FromInt(InstructionIndex);
			while (ResultIndexStr.Len() < 3)
			{
				ResultIndexStr = TEXT("0") + ResultIndexStr;
			}
			Result.Add(FString::Printf(TEXT("%s. %s"), *ResultIndexStr, *ResultLine));
		}
		else
		{
			Result.Add(ResultLine);
		}
	}

	return Result;
}

FString URigVM::DumpByteCodeAsText(const TArray<int32>& InInstructionOrder, bool bIncludeLineNumbers)
{
	return FString::Join(DumpByteCodeAsTextArray(InInstructionOrder, bIncludeLineNumbers), TEXT("\n"));
}

FString URigVM::GetOperandLabel(const FRigVMOperand& InOperand) const
{
	const FRigVMMemoryContainer* MemoryPtr = nullptr;

	if (InOperand.GetMemoryType() == ERigVMMemoryType::Literal)
	{
		MemoryPtr = LiteralMemoryPtr;
	}
	else
	{
		MemoryPtr = WorkMemoryPtr;
	}

	const FRigVMMemoryContainer& Memory = *MemoryPtr;

	FString OperandLabel;
	if (InOperand.GetMemoryType() == ERigVMMemoryType::External)
	{
		const FRigVMExternalVariable& ExternalVariable = ExternalVariables[InOperand.GetRegisterIndex()];
		OperandLabel = FString::Printf(TEXT("Variable::%s"), *ExternalVariable.Name.ToString());
	}
	else
	{
		FRigVMRegister Register = Memory[InOperand];
		OperandLabel = Register.Name.ToString();
	}

	if (InOperand.GetRegisterOffset() != INDEX_NONE)
	{
		return FString::Printf(TEXT("%s.%s"), *OperandLabel, *Memory.RegisterOffsets[InOperand.GetRegisterOffset()].CachedSegmentPath);
	}

	return OperandLabel;
}

#endif

void URigVM::CacheSingleMemoryHandle(const FRigVMOperand& InArg, bool bForExecute)
{
	if (InArg.GetMemoryType() == ERigVMMemoryType::External)
	{
		ensure(ExternalVariables.IsValidIndex(InArg.GetRegisterIndex()));

		FRigVMExternalVariable& ExternalVariable = ExternalVariables[InArg.GetRegisterIndex()];
		check(ExternalVariable.Memory);

		FRigVMMemoryHandle Handle = ExternalVariable.GetHandle();
		if (InArg.GetRegisterOffset() != INDEX_NONE)
		{
			FRigVMMemoryContainer* WorkMemory = CachedMemory[(int32)ERigVMMemoryType::Work];
			const FRigVMRegisterOffset& RegisterOffset = WorkMemory->RegisterOffsets[InArg.GetRegisterOffset()];

			// offset the handle to the memory based on the register offset
			uint8* Ptr = RegisterOffset.GetData(Handle.GetData());
			Handle = FRigVMMemoryHandle(Ptr, RegisterOffset.GetElementSize(), FRigVMMemoryHandle::FType::Plain);
		}
		CachedMemoryHandles.Add(Handle);
		return;
	}

	const FRigVMRegister& Register = CachedMemory[InArg.GetContainerIndex()]->GetRegister(InArg);
	
	CachedMemoryHandles.Add(CachedMemory[InArg.GetContainerIndex()]->GetHandle(Register, InArg.GetRegisterOffset()));

	if (bForExecute)
	{
		if (Register.IsArray() && !Register.IsDynamic())
	{
			void* ElementsForArray = reinterpret_cast<void*>(Register.ElementCount);
			CachedMemoryHandles.Add(FRigVMMemoryHandle((uint8*)ElementsForArray));
		}
	}
}
