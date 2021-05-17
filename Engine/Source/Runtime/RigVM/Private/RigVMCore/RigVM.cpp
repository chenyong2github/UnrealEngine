// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVM.h"
#include "UObject/Package.h"
#include "UObject/AnimObjectVersion.h"
#include "HAL/PlatformTLS.h"

void FRigVMParameter::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FAnimObjectVersion::GUID);

	if (Ar.CustomVer(FAnimObjectVersion::GUID) < FAnimObjectVersion::StoreMarkerNamesOnSkeleton)
	{
		return;
	}

	if (Ar.IsSaving() || Ar.IsObjectReferenceCollector() || Ar.IsCountingMemory())
	{
		Save(Ar);
	}
	else if (Ar.IsLoading())
	{
		Load(Ar);
	}
	else
	{
		checkNoEntry();
	}
}

void FRigVMParameter::Save(FArchive& Ar)
{
	Ar << Type;
	Ar << Name;
	Ar << RegisterIndex;
	Ar << CPPType;
	Ar << ScriptStructPath;
}

void FRigVMParameter::Load(FArchive& Ar)
{
	Ar << Type;
	Ar << Name;
	Ar << RegisterIndex;
	Ar << CPPType;
	Ar << ScriptStructPath;

	ScriptStruct = nullptr;
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
    , DebugMemoryPtr(&DebugMemoryStorage)
    , ByteCodePtr(&ByteCodeStorage)
#if WITH_EDITOR
	, DebugInfo(nullptr)
	, HaltedAtInstruction(INDEX_NONE)
	, HaltedAtInstructionHit(INDEX_NONE)
#endif
    , FunctionNamesPtr(&FunctionNamesStorage)
    , FunctionsPtr(&FunctionsStorage)
#if WITH_EDITOR
	, FirstEntryEventInQueue(NAME_None)
#endif
	, ExecutingThreadId(INDEX_NONE)
	, DeferredVMToCopy(nullptr)
{
	GetWorkMemory().SetMemoryType(ERigVMMemoryType::Work);
	GetLiteralMemory().SetMemoryType(ERigVMMemoryType::Literal);
	GetDebugMemory().SetMemoryType(ERigVMMemoryType::Debug);
}

URigVM::~URigVM()
{
	Reset();

	ExecutionReachedExit().Clear();
#if WITH_EDITOR
	ExecutionHalted().Clear();
#endif
}

void URigVM::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FAnimObjectVersion::GUID);

	if (Ar.CustomVer(FAnimObjectVersion::GUID) < FAnimObjectVersion::StoreMarkerNamesOnSkeleton)
	{
		return;
	}

	ensure(ExecutingThreadId == INDEX_NONE);

	if (Ar.IsSaving() || Ar.IsObjectReferenceCollector() || Ar.IsCountingMemory())
	{
		Save(Ar);
	}
	else if (Ar.IsLoading())
	{
		Load(Ar);
	}
	else
	{
		checkNoEntry();
	}
}

void URigVM::Save(FArchive& Ar)
{
	CopyDeferredVMIfRequired();

	Ar << WorkMemoryStorage;
	Ar << LiteralMemoryStorage;
	Ar << FunctionNamesStorage;
	Ar << ByteCodeStorage;
	Ar << Parameters;
}

void URigVM::Load(FArchive& Ar)
{
	Reset();

	Ar << WorkMemoryStorage;
	Ar << LiteralMemoryStorage;
	Ar << FunctionNamesStorage;
	Ar << ByteCodeStorage;
	Ar << Parameters;

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

void URigVM::Reset()
{
	WorkMemoryStorage.Reset();
	LiteralMemoryStorage.Reset();
	DebugMemoryStorage.Reset();
	FunctionNamesStorage.Reset();
	FunctionsStorage.Reset();
	ByteCodeStorage.Reset();
	Instructions.Reset();
	Parameters.Reset();
	ParametersNameMap.Reset();
	DeferredVMToCopy = nullptr;

	WorkMemoryPtr = &WorkMemoryStorage;
	LiteralMemoryPtr = &LiteralMemoryStorage;
	DebugMemoryPtr = &DebugMemoryStorage;
	FunctionNamesPtr = &FunctionNamesStorage;
	FunctionsPtr = &FunctionsStorage;
	ByteCodePtr = &ByteCodeStorage;

	InvalidateCachedMemory();
	
	OperandToDebugRegisters.Reset();
}

void URigVM::Empty()
{
	WorkMemoryStorage.Empty();
	LiteralMemoryStorage.Empty();
	DebugMemoryStorage.Empty();
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

	OperandToDebugRegisters.Empty();
}

void URigVM::CopyFrom(URigVM* InVM, bool bDeferCopy, bool bReferenceLiteralMemory, bool bReferenceByteCode, bool bCopyExternalVariables, bool bCopyDynamicRegisters)
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
		if (bCopyDynamicRegisters)
		{
			WorkMemoryStorage.CopyRegisters(InVM->WorkMemoryStorage);
		}
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

	if(InVM->DebugMemoryPtr == &InVM->DebugMemoryStorage)
	{
		DebugMemoryStorage = InVM->DebugMemoryStorage;
		DebugMemoryPtr = &DebugMemoryStorage;
	}
	else
	{
		DebugMemoryPtr = InVM->DebugMemoryPtr;
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
	OperandToDebugRegisters = InVM->OperandToDebugRegisters;

	if (bCopyExternalVariables)
	{
		ExternalVariables = InVM->ExternalVariables;
	}
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

#if WITH_EDITOR
bool URigVM::ResumeExecution()
{
	HaltedAtInstruction = INDEX_NONE;
	HaltedAtInstructionHit = INDEX_NONE;
	if (DebugInfo)
	{
		if (FRigVMBreakpoint* Breakpoint = DebugInfo->FindBreakpoint(Context.InstructionIndex))
		{
			DebugInfo->IncrementBreakpointActivationOnHit(Context.InstructionIndex);
			return true;
		}
	}

	return false;
}

bool URigVM::ResumeExecution(FRigVMMemoryContainerPtrArray Memory, FRigVMFixedArray<void*> AdditionalArguments, const FName& InEntryName)
{
	ResumeExecution();
	return Execute(Memory, AdditionalArguments, InEntryName);
}

#endif

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

#if WITH_EDITOR
bool URigVM::ShouldHaltAtInstruction(const uint16 InstructionIndex)
{
	FRigVMByteCode& ByteCode = GetByteCode();
	
	if (FRigVMBreakpoint* Breakpoint = DebugInfo->FindBreakpoint(Context.InstructionIndex))
	{
		if (DebugInfo->IsActive(Context.InstructionIndex))
		{
			switch (CurrentBreakpointAction)
			{
				case ERigVMBreakpointAction::None:
				{
					// Halted at breakpoint. Check if this is a new breakpoint different from the previous halt.
					if (HaltedAtInstruction != Context.InstructionIndex ||
						HaltedAtInstructionHit != DebugInfo->GetBreakpointHits(Context.InstructionIndex))
					{
						HaltedAtInstruction = Context.InstructionIndex;
						HaltedAtInstructionHit = DebugInfo->GetBreakpointHits(HaltedAtInstruction);
						ExecutionHalted().Broadcast(Context.InstructionIndex, Breakpoint->Subject);
					}
					return true;
				}
				case ERigVMBreakpointAction::Resume:
				{
					CurrentBreakpointAction = ERigVMBreakpointAction::None;

					if (DebugInfo->IsTemporaryBreakpoint(Breakpoint))
					{
						DebugInfo->RemoveBreakpoint(Breakpoint->InstructionIndex);
					}
					else
					{
						DebugInfo->IncrementBreakpointActivationOnHit(Context.InstructionIndex);
						DebugInfo->HitBreakpoint(Context.InstructionIndex);
					}
					return false;
					break;
				}
				case ERigVMBreakpointAction::StepOver:
				case ERigVMBreakpointAction::StepInto:
				case ERigVMBreakpointAction::StepOut:
				{
					// If we are stepping, check if we were halted at the current instruction, and remember it 
					if (!DebugInfo->GetSteppingOriginBreakpoint())
					{
						DebugInfo->SetSteppingOriginBreakpoint(Breakpoint);
						const TArray<UObject*>* FullCallstack = ByteCode.GetCallstackForInstruction(Context.InstructionIndex);
						
						// We want to keep the callstack up to the node that produced the halt
						DebugInfo->SetSteppingOriginBreakpointCallstack(TArray<UObject*>(FullCallstack->GetData(), FullCallstack->Find((UObject*)DebugInfo->GetSteppingOriginBreakpoint()->Subject)+1));
					}							
					
					break;	
				}
				default:
				{
					ensure(false);
					break;
				}
			}
		}
		else
		{
			DebugInfo->HitBreakpoint(Context.InstructionIndex);
		}
	}

	// If we are stepping, and the last active breakpoint was set, check if this is the new temporary breakpoint
	if (DebugInfo->GetSteppingOriginBreakpoint())
	{
		const TArray<UObject*>* CurrentCallstack = ByteCode.GetCallstackForInstruction(Context.InstructionIndex);
		if (CurrentCallstack && !CurrentCallstack->IsEmpty())
		{
			UObject* NewBreakpointNode = nullptr;

			// Find the first difference in the callstack
			int32 DifferenceIndex = INDEX_NONE;
			TArray<UObject*>& PreviousCallstack = DebugInfo->GetSteppingOriginBreakpointCallstack();
			for (int32 i=0; i<PreviousCallstack.Num(); ++i)
			{
				if (CurrentCallstack->Num() == i)
				{
					DifferenceIndex = i-1;
					break;
				}
				if (PreviousCallstack[i] != CurrentCallstack->operator[](i))
				{
					DifferenceIndex = i;
					break;
				}
			}

			if (CurrentBreakpointAction == ERigVMBreakpointAction::StepOver)
			{
				if (DifferenceIndex != INDEX_NONE)
				{
					NewBreakpointNode = CurrentCallstack->operator[](DifferenceIndex);
				}
			}
			else if (CurrentBreakpointAction == ERigVMBreakpointAction::StepInto)
			{
				if (DifferenceIndex == INDEX_NONE)
				{
					if (CurrentCallstack->Last() != PreviousCallstack.Last())
					{
						NewBreakpointNode = CurrentCallstack->operator[](FMath::Min(PreviousCallstack.Num(), CurrentCallstack->Num()-1));
					}
				}
				else
				{
					NewBreakpointNode = CurrentCallstack->operator[](DifferenceIndex);
				}
			}
			else if (CurrentBreakpointAction == ERigVMBreakpointAction::StepOut)
			{
				if (DifferenceIndex != INDEX_NONE && DifferenceIndex <= PreviousCallstack.Num() - 2)
                {
                	NewBreakpointNode = CurrentCallstack->operator[](DifferenceIndex);
                }
			}
			
			if (NewBreakpointNode)
			{
				if (DebugInfo->IsTemporaryBreakpoint(DebugInfo->GetSteppingOriginBreakpoint()))
				{
					DebugInfo->RemoveBreakpoint(DebugInfo->GetSteppingOriginBreakpoint()->InstructionIndex);
				}
				else
				{
					DebugInfo->IncrementBreakpointActivationOnHit(DebugInfo->GetSteppingOriginBreakpoint()->InstructionIndex);
					DebugInfo->HitBreakpoint(DebugInfo->GetSteppingOriginBreakpoint()->InstructionIndex);
				}
				
				FRigVMBreakpoint* NewBreakpoint = DebugInfo->AddBreakpoint(Context.InstructionIndex, NewBreakpointNode, true);
				CurrentBreakpointAction = ERigVMBreakpointAction::None;					

				// Halted at breakpoint. Check if this is a new breakpoint different from the previous halt.
				HaltedAtInstruction = Context.InstructionIndex;
				HaltedAtInstructionHit = DebugInfo->GetBreakpointHits(HaltedAtInstruction);
				ExecutionHalted().Broadcast(Context.InstructionIndex, NewBreakpointNode);
		
				return true;
			}
		}
	}

	return false;
}
#endif

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

	FRigVMMemoryContainer* LocalMemory[] = { WorkMemoryPtr, LiteralMemoryPtr, DebugMemoryPtr };
	if (Memory.Num() == 0)
	{
		Memory = FRigVMMemoryContainerPtrArray(LocalMemory, 3);
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

	FRigVMMemoryContainer* LocalMemory[] = { WorkMemoryPtr, LiteralMemoryPtr, DebugMemoryPtr }; 
	if (Memory.Num() == 0)
	{
		Memory = FRigVMMemoryContainerPtrArray(LocalMemory, 3);
	}

	CacheMemoryHandlesIfRequired(Memory);
	FRigVMByteCode& ByteCode = GetByteCode();
	TArray<FRigVMFunctionPtr>& Functions = GetFunctions();

#if WITH_EDITOR
	TArray<FName>& FunctionNames = GetFunctionNames();

	if (FirstEntryEventInQueue == NAME_None || FirstEntryEventInQueue == InEntryName)
	{
		InstructionVisitedDuringLastRun.Reset();
		InstructionVisitOrder.Reset();
		InstructionVisitedDuringLastRun.SetNumZeroed(Instructions.Num());
	}
#endif

	Context.Reset();
	Context.SliceOffsets.AddZeroed(Instructions.Num());
	Context.OpaqueArguments = AdditionalArguments;
	Context.ExternalVariables = ExternalVariables;

	ClearDebugMemory();

	if (!InEntryName.IsNone())
	{
		int32 EntryIndex = ByteCode.FindEntryIndex(InEntryName);
		if (EntryIndex == INDEX_NONE)
		{
			return false;
		}
		Context.InstructionIndex = (uint16)ByteCode.GetEntry(EntryIndex).InstructionIndex;
	}

#if WITH_EDITOR
	if (DebugInfo)
	{
		DebugInfo->StartExecution();
	}
#endif

	while (Instructions.IsValidIndex(Context.InstructionIndex))
	{
#if WITH_EDITOR
		if (DebugInfo && ShouldHaltAtInstruction(Context.InstructionIndex))
		{
			return true;
		}
		
		InstructionVisitedDuringLastRun[Context.InstructionIndex]++;
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
				const int32 OperandCount = FirstHandleForInstruction[Context.InstructionIndex + 1] - FirstHandleForInstruction[Context.InstructionIndex];
				FRigVMMemoryHandleArray Handles(&CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]], OperandCount);
#if WITH_EDITOR
				Context.FunctionName = FunctionNames[Op.FunctionIndex];
#endif
				(*Functions[Op.FunctionIndex])(Context, Handles);

#if WITH_EDITOR
				if(DebugMemoryPtr->Num() > 0)
				{
					const FRigVMOperandArray Operands = ByteCode.GetOperandsForExecuteOp(Instruction);
					for(int32 OperandIndex = 0, HandleIndex = 0; OperandIndex < Operands.Num() && HandleIndex < Handles.Num(); HandleIndex++)
					{
						// skip array sizes
						if(Handles[HandleIndex].GetType() == FRigVMMemoryHandle::FType::ArraySize)
						{
							continue;
						}
						CopyOperandForDebuggingIfNeeded(Operands[OperandIndex++], Handles[HandleIndex]);
					}
				}
#endif

				Context.InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Zero:
			{
				*((int32*)CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]].GetData()) = 0;
#if WITH_EDITOR
				if(DebugMemoryPtr->Num() > 0)
				{
					const FRigVMUnaryOp& Op = ByteCode.GetOpAt<FRigVMUnaryOp>(Instruction);
					CopyOperandForDebuggingIfNeeded(Op.Arg, CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]]);
				}
#endif

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

				const uint64 NumBytes = reinterpret_cast<uint64>(CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex] + 2].GetData());
				const ERigVMRegisterType MemoryType = (ERigVMRegisterType)reinterpret_cast<uint64>(CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex] + 3].GetData());

				if (TargetHandle.Type == FRigVMMemoryHandle::Dynamic)
				{
					FRigVMByteArray* Storage = (FRigVMByteArray*)TargetHandle.Ptr;
					if (Context.GetSlice().GetIndex() == 0)
					{
						Storage->Reset();
					}
					const int32 ByteIndex = Storage->AddZeroed(NumBytes);
					TargetPtr = Storage->GetData() + ByteIndex;
				}
				else if (TargetHandle.Type == FRigVMMemoryHandle::NestedDynamic)
				{
					FRigVMNestedByteArray* Storage = (FRigVMNestedByteArray*)TargetHandle.Ptr;
					if (Context.GetSlice().GetIndex() == 0)
					{
						Storage->Reset();
					}
					const int32 ArrayIndex = Storage->Add(FRigVMByteArray());
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
						const int32 NumNames = NumBytes / sizeof(FName);
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
						const int32 NumStrings = NumBytes / sizeof(FString);
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
						const int32 NumStructs = NumBytes / ScriptStruct->GetStructureSize();
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
#if WITH_EDITOR
				if(DebugMemoryPtr->Num() > 0)
				{
					const FRigVMUnaryOp& Op = ByteCode.GetOpAt<FRigVMUnaryOp>(Instruction);
					CopyOperandForDebuggingIfNeeded(Op.Arg, CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]]);
				}
#endif
				Context.InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Decrement:
			{
				(*((int32*)CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]].GetData()))--;
#if WITH_EDITOR
				if(DebugMemoryPtr->Num() > 0)
				{
					const FRigVMUnaryOp& Op = ByteCode.GetOpAt<FRigVMUnaryOp>(Instruction);
					CopyOperandForDebuggingIfNeeded(Op.Arg, CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]]);
				}
#endif
				Context.InstructionIndex++;
				break;
			}
			case ERigVMOpCode::Equals:
			case ERigVMOpCode::NotEquals:
			{
				const FRigVMComparisonOp& Op = ByteCode.GetOpAt<FRigVMComparisonOp>(Instruction);
				const FRigVMRegister& RegisterA = (*Memory[Op.A.GetContainerIndex()])[Op.A.GetRegisterIndex()];
				const FRigVMRegister& RegisterB = (*Memory[Op.B.GetContainerIndex()])[Op.B.GetRegisterIndex()];
				const uint16 BytesA = RegisterA.GetNumBytesPerSlice();
				const uint16 BytesB = RegisterB.GetNumBytesPerSlice();
				
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
				const bool Condition = *(bool*)CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]].GetData();
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
				const bool Condition = *(bool*)CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]].GetData();
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
				const bool Condition = *(bool*)CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]].GetData();
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
				ExecutionReachedExit().Broadcast();
#if WITH_EDITOR					
				if (HaltedAtInstruction != INDEX_NONE)
				{
					HaltedAtInstruction = INDEX_NONE;
					ExecutionHalted().Broadcast(INDEX_NONE, nullptr);
				}
#endif
				return true;
			}
			case ERigVMOpCode::BeginBlock:
			{
				const int32 Count = (*((int32*)CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex]].GetData()));
				const int32 Index = (*((int32*)CachedMemoryHandles[FirstHandleForInstruction[Context.InstructionIndex] + 1].GetData()));
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

#if WITH_EDITOR
	if (HaltedAtInstruction != INDEX_NONE)
	{
		HaltedAtInstruction = INDEX_NONE;
		ExecutionHalted().Broadcast(INDEX_NONE, nullptr);
	}
#endif

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
	else if (InOperand.GetMemoryType() == ERigVMMemoryType::Debug)
	{
		GetDebugMemory().SetRegisterValueFromString(InOperand, InCPPType, InCPPTypeObject, InDefaultValues);
	}
}


#if WITH_EDITOR

TArray<FString> URigVM::DumpByteCodeAsTextArray(const TArray<int32>& InInstructionOrder, bool bIncludeLineNumbers, TFunction<FString(const FString& RegisterName, const FString& RegisterOffsetName)> OperandFormatFunction)
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
					Labels.Add(GetOperandLabel(Operand, OperandFormatFunction));
				}

				ResultLine = FString::Printf(TEXT("%s(%s)"), *FunctionName, *FString::Join(Labels, TEXT(",")));
				break;
			}
			case ERigVMOpCode::Zero:
			{
				const FRigVMUnaryOp& Op = ByteCode.GetOpAt<FRigVMUnaryOp>(Instructions[InstructionIndex]);
				ResultLine = FString::Printf(TEXT("Set %s to 0"), *GetOperandLabel(Op.Arg, OperandFormatFunction));
				break;
			}
			case ERigVMOpCode::BoolFalse:
			{
				const FRigVMUnaryOp& Op = ByteCode.GetOpAt<FRigVMUnaryOp>(Instructions[InstructionIndex]);
				ResultLine = FString::Printf(TEXT("Set %s to False"), *GetOperandLabel(Op.Arg, OperandFormatFunction));
				break;
			}
			case ERigVMOpCode::BoolTrue:
			{
				const FRigVMUnaryOp& Op = ByteCode.GetOpAt<FRigVMUnaryOp>(Instructions[InstructionIndex]);
				ResultLine = FString::Printf(TEXT("Set %s to True"), *GetOperandLabel(Op.Arg, OperandFormatFunction));
				break;
			}
			case ERigVMOpCode::Increment:
			{
				const FRigVMUnaryOp& Op = ByteCode.GetOpAt<FRigVMUnaryOp>(Instructions[InstructionIndex]);
				ResultLine = FString::Printf(TEXT("Inc %s ++"), *GetOperandLabel(Op.Arg, OperandFormatFunction));
				break;
			}
			case ERigVMOpCode::Decrement:
			{
				const FRigVMUnaryOp& Op = ByteCode.GetOpAt<FRigVMUnaryOp>(Instructions[InstructionIndex]);
				ResultLine = FString::Printf(TEXT("Dec %s --"), *GetOperandLabel(Op.Arg, OperandFormatFunction));
				break;
			}
			case ERigVMOpCode::Copy:
			{
				const FRigVMCopyOp& Op = ByteCode.GetOpAt<FRigVMCopyOp>(Instructions[InstructionIndex]);
				ResultLine = FString::Printf(TEXT("Copy %s to %s"), *GetOperandLabel(Op.Source, OperandFormatFunction), *GetOperandLabel(Op.Target, OperandFormatFunction));
				break;
			}
			case ERigVMOpCode::Equals:
			{
				const FRigVMComparisonOp& Op = ByteCode.GetOpAt<FRigVMComparisonOp>(Instructions[InstructionIndex]);
				ResultLine = FString::Printf(TEXT("Set %s to %s == %s "), *GetOperandLabel(Op.Result, OperandFormatFunction), *GetOperandLabel(Op.A, OperandFormatFunction), *GetOperandLabel(Op.B, OperandFormatFunction));
				break;
			}
			case ERigVMOpCode::NotEquals:
			{
				const FRigVMComparisonOp& Op = ByteCode.GetOpAt<FRigVMComparisonOp>(Instructions[InstructionIndex]);
				ResultLine = FString::Printf(TEXT("Set %s to %s != %s"), *GetOperandLabel(Op.Result, OperandFormatFunction), *GetOperandLabel(Op.A, OperandFormatFunction), *GetOperandLabel(Op.B, OperandFormatFunction));
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
					ResultLine = FString::Printf(TEXT("Jump to instruction %d if %s"), Op.InstructionIndex, *GetOperandLabel(Op.Arg, OperandFormatFunction));
				}
				else
				{
					ResultLine = FString::Printf(TEXT("Jump to instruction %d if !%s"), Op.InstructionIndex, *GetOperandLabel(Op.Arg, OperandFormatFunction));
				}
				break;
			}
			case ERigVMOpCode::JumpForwardIf:
			{
				const FRigVMJumpIfOp& Op = ByteCode.GetOpAt<FRigVMJumpIfOp>(Instructions[InstructionIndex]);
				if (Op.Condition)
				{
					ResultLine = FString::Printf(TEXT("Jump %d instructions forwards if %s"), Op.InstructionIndex, *GetOperandLabel(Op.Arg, OperandFormatFunction));
				}
				else
				{
					ResultLine = FString::Printf(TEXT("Jump %d instructions forwards if !%s"), Op.InstructionIndex, *GetOperandLabel(Op.Arg, OperandFormatFunction));
				}
				break;
			}
			case ERigVMOpCode::JumpBackwardIf:
			{
				const FRigVMJumpIfOp& Op = ByteCode.GetOpAt<FRigVMJumpIfOp>(Instructions[InstructionIndex]);
				if (Op.Condition)
				{
					ResultLine = FString::Printf(TEXT("Jump %d instructions backwards if %s"), Op.InstructionIndex, *GetOperandLabel(Op.Arg, OperandFormatFunction));
				}
				else
				{
					ResultLine = FString::Printf(TEXT("Jump %d instructions backwards if !%s"), Op.InstructionIndex, *GetOperandLabel(Op.Arg, OperandFormatFunction));
				}
				break;
			}
			case ERigVMOpCode::ChangeType:
			{
				const FRigVMChangeTypeOp& Op = ByteCode.GetOpAt<FRigVMChangeTypeOp>(Instructions[InstructionIndex]);
				ResultLine = FString::Printf(TEXT("Change type of %s"), *GetOperandLabel(Op.Arg, OperandFormatFunction));
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

FString URigVM::GetOperandLabel(const FRigVMOperand& InOperand, TFunction<FString(const FString& RegisterName, const FString& RegisterOffsetName)> FormatFunction) const
{
	const FRigVMMemoryContainer* MemoryPtr = nullptr;

	if (InOperand.GetMemoryType() == ERigVMMemoryType::Literal)
	{
		MemoryPtr = LiteralMemoryPtr;
	}
	else if (InOperand.GetMemoryType() == ERigVMMemoryType::Debug)
	{
		MemoryPtr = DebugMemoryPtr;
	}
	else
	{
		MemoryPtr = WorkMemoryPtr;
	}

	const FRigVMMemoryContainer& Memory = *MemoryPtr;

	FString RegisterName;
	if (InOperand.GetMemoryType() == ERigVMMemoryType::External)
	{
		const FRigVMExternalVariable& ExternalVariable = ExternalVariables[InOperand.GetRegisterIndex()];
		RegisterName = FString::Printf(TEXT("Variable::%s"), *ExternalVariable.Name.ToString());
	}
	else
	{
		FRigVMRegister Register = Memory[InOperand];
		RegisterName = Register.Name.ToString();
	}

	FString OperandLabel;
	OperandLabel = RegisterName;
	
	// append an offset name if it exists
	FString RegisterOffsetName;
	if (InOperand.GetRegisterOffset() != INDEX_NONE)
	{
		RegisterOffsetName = Memory.RegisterOffsets[InOperand.GetRegisterOffset()].CachedSegmentPath;
		OperandLabel = FString::Printf(TEXT("%s.%s"), *OperandLabel, *RegisterOffsetName);
	}

	// caller can provide an alternative format to override the default format(optional)
	if (FormatFunction)
	{
		OperandLabel = FormatFunction(RegisterName, RegisterOffsetName);
	}

	return OperandLabel;
}

#endif

void URigVM::ClearDebugMemory()
{
#if WITH_EDITOR
	FRigVMMemoryContainer* DebugMemory = CachedMemory[(int32)ERigVMMemoryType::Debug];
	if (DebugMemory)
	{ 
		for (int32 RegisterIndex = 0; RegisterIndex < DebugMemory->Num(); RegisterIndex++)
		{
			ensure(DebugMemory->GetRegister(RegisterIndex).IsDynamic());
			DebugMemory->Destroy(RegisterIndex);
		}	
	} 
#endif
}

void URigVM::CacheSingleMemoryHandle(const FRigVMOperand& InArg, bool bForExecute)
{
	if (InArg.GetMemoryType() == ERigVMMemoryType::External)
	{
		ensure(ExternalVariables.IsValidIndex(InArg.GetRegisterIndex()));

		FRigVMExternalVariable& ExternalVariable = ExternalVariables[InArg.GetRegisterIndex()];
		const FRigVMRegisterOffset& RegisterOffset = GetWorkMemory().GetRegisterOffsetForOperand(InArg);
		check(ExternalVariable.Memory);

		FRigVMMemoryHandle Handle = ExternalVariable.GetHandle();
		if (RegisterOffset.IsValid())
		{
			Handle.RegisterOffset = &RegisterOffset;
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
			CachedMemoryHandles.Add(FRigVMMemoryHandle((uint8*)ElementsForArray, sizeof(uint16), FRigVMMemoryHandle::FType::ArraySize));
		}
	}
}

void URigVM::CopyOperandForDebuggingImpl(const FRigVMOperand& InArg, const FRigVMMemoryHandle& InHandle, const FRigVMOperand& InDebugOperand)
{
#if WITH_EDITOR
	check(InArg.IsValid());
	check(InArg.GetRegisterOffset() == INDEX_NONE);
	check(InDebugOperand.IsValid());
	check(InDebugOperand.GetRegisterOffset() == INDEX_NONE);

	const FRigVMRegister& DebugRegister = DebugMemoryPtr->GetRegister(InDebugOperand.GetRegisterIndex());
	check(DebugRegister.IsDynamic());

	if (Context.GetSlice().GetIndex() == 0)
	{
		DebugMemoryPtr->Destroy(InDebugOperand.GetRegisterIndex());
	}

	// the source pointer is not going to be sliced since we only allow
	// watches on things exposed from a node (so no hidden pins)
	const uint8* SourcePtr = InHandle.GetData(0, true);
	uint8* TargetPtr = nullptr;

	int32 NumBytes = (int32)DebugRegister.ElementSize;
	if(InHandle.GetType() == FRigVMMemoryHandle::FType::Dynamic)
	{
		FRigVMByteArray* Storage = (FRigVMByteArray*)InHandle.Ptr;
		NumBytes = Storage->Num();
		TargetPtr = Storage->GetData();
	}
	else if(InHandle.GetType() == FRigVMMemoryHandle::FType::NestedDynamic)
	{
		FRigVMNestedByteArray* Storage = (FRigVMNestedByteArray*)InHandle.Ptr;
		NumBytes = (*Storage)[Context.GetSlice().GetIndex()].Num(); 
		TargetPtr = (*Storage)[Context.GetSlice().GetIndex()].GetData();
	}

	const FRigVMMemoryHandle DebugHandle = DebugMemoryPtr->GetHandle(InDebugOperand.GetRegisterIndex());
	if (DebugRegister.IsNestedDynamic())
	{
		FRigVMNestedByteArray* Storage = (FRigVMNestedByteArray*)DebugHandle.Ptr;
		while(Storage->Num() < Context.GetSlice().TotalNum())
		{
			Storage->Add(FRigVMByteArray());
		}
		(*Storage)[Context.GetSlice().GetIndex()].AddZeroed(NumBytes);
		TargetPtr = (*Storage)[Context.GetSlice().GetIndex()].GetData();
	}
	else
	{
		const int32 TotalBytes = Context.GetSlice().TotalNum() * NumBytes;
		FRigVMByteArray* Storage = (FRigVMByteArray*)DebugHandle.Ptr;
		while(Storage->Num() < TotalBytes)
		{
			Storage->AddZeroed(NumBytes);
		}
		TargetPtr = &(*Storage)[Context.GetSlice().GetIndex() * NumBytes];
	}

	if((SourcePtr == nullptr) || (TargetPtr == nullptr))
	{
		return;
	}

	switch (DebugRegister.Type)
	{
		case ERigVMRegisterType::Plain:
		{
			FMemory::Memcpy(TargetPtr, SourcePtr, NumBytes);
			break;
		}
		case ERigVMRegisterType::Name:
		{
			const int32 NumNames = NumBytes / sizeof(FName);
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
			const int32 NumStrings = NumBytes / sizeof(FString);
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
			UScriptStruct* ScriptStruct = DebugMemoryPtr->GetScriptStruct(DebugRegister);
			const int32 NumStructs = NumBytes / ScriptStruct->GetStructureSize();
			if (NumStructs > 0 && TargetPtr)
			{
				ScriptStruct->CopyScriptStruct(TargetPtr, SourcePtr, NumStructs);
			}
			break;
		}
		default:
		{
			// the default pass for any complex memory
			FRigVMMemoryContainer* LocalMemory[] = { WorkMemoryPtr, LiteralMemoryPtr, DebugMemoryPtr };
			DebugMemoryPtr->Copy(InArg, InDebugOperand, LocalMemory[InArg.GetContainerIndex()]);
			break;
		}
	}

	
#endif
}
