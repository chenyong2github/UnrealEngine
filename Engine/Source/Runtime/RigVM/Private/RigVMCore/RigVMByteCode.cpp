// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVMByteCode.h"
#include "UObject/AnimObjectVersion.h"
#include "UObject/FortniteMainBranchObjectVersion.h"

bool FRigVMExecuteOp::Serialize(FArchive& Ar)
{
	Ar << OpCode;
	Ar << FunctionIndex;
	return true;
}

bool FRigVMUnaryOp::Serialize(FArchive& Ar)
{
	Ar << OpCode;
	Ar << Arg;
	return true;
}

bool FRigVMBinaryOp::Serialize(FArchive& Ar)
{
	Ar << OpCode;
	Ar << ArgA;
	Ar << ArgB;
	return true;
}

bool FRigVMCopyOp::Serialize(FArchive& Ar)
{
	Ar << OpCode;
	Ar << Source;
	Ar << Target;
	return true;
}

bool FRigVMComparisonOp::Serialize(FArchive& Ar)
{
	Ar << OpCode;
	Ar << A;
	Ar << B;
	Ar << Result;
	return true;
}

bool FRigVMJumpOp::Serialize(FArchive& Ar)
{
	Ar << OpCode;
	Ar << InstructionIndex;
	return true;
}

bool FRigVMJumpIfOp::Serialize(FArchive& Ar)
{
	Ar << OpCode;
	Ar << Arg;
	Ar << InstructionIndex;
	Ar << Condition;
	return true;
}

bool FRigVMChangeTypeOp::Serialize(FArchive& Ar)
{
	ensure(false);
	return false;
}

FRigVMInstructionArray::FRigVMInstructionArray()
{
}

FRigVMInstructionArray::FRigVMInstructionArray(const FRigVMByteCode& InByteCode, bool bByteCodeIsAligned)
{
	uint64 ByteIndex = 0;
	while (ByteIndex < InByteCode.Num())
	{
		ERigVMOpCode OpCode = InByteCode.GetOpCodeAt(ByteIndex);
		if ((int32)OpCode >= (int32)ERigVMOpCode::Invalid)
		{
			checkNoEntry();
			Instructions.Reset();
			break;
		}

		uint8 OperandAlignment = 0;

		if (bByteCodeIsAligned)
		{
			uint64 Alignment = InByteCode.GetOpAlignment(OpCode);
			if (Alignment > 0)
			{
				while (!IsAligned(&InByteCode[ByteIndex], Alignment))
				{
					ByteIndex++;
				}
			}

			if (OpCode >= ERigVMOpCode::Execute_0_Operands && OpCode < ERigVMOpCode::Execute_64_Operands)
			{
				uint64 OperandByteIndex = ByteIndex + (uint64)sizeof(FRigVMExecuteOp);

				Alignment = InByteCode.GetOperandAlignment();
				if (Alignment > 0)
				{
					while (!IsAligned(&InByteCode[OperandByteIndex + OperandAlignment], Alignment))
					{
						OperandAlignment++;
					}
				}
			}
		}

		Instructions.Add(FRigVMInstruction(OpCode, ByteIndex, OperandAlignment));
		ByteIndex += InByteCode.GetOpNumBytesAt(ByteIndex, true);
	}
}

void FRigVMInstructionArray::Reset()
{
	Instructions.Reset();
}

void FRigVMInstructionArray::Empty()
{
	Instructions.Empty();
}

FRigVMByteCode::FRigVMByteCode()
	: NumInstructions(0)
	, bByteCodeIsAligned(false)
{
}

bool FRigVMByteCode::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FAnimObjectVersion::GUID);
	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);

	if (Ar.CustomVer(FAnimObjectVersion::GUID) < FAnimObjectVersion::StoreMarkerNamesOnSkeleton)
	{
		// return false to skip the section in the archive
		return false;
	}

	if (Ar.IsLoading() && Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::RigVMByteCodeDeterminism)
	{
		Ar << ByteCode;
		return true;
	}

	FRigVMInstructionArray Instructions;
	
	int32 InstructionCount = 0;
	if (Ar.IsSaving())
	{
		Instructions = GetInstructions();
		InstructionCount = Instructions.Num();
	}
	else
	{
		// during reference collection we don't reset the bytecode.
		if (Ar.IsObjectReferenceCollector())
		{
			return false;
		}

		ByteCode.Reset();
		bByteCodeIsAligned = false;
	}

	Ar << InstructionCount;

	for (int32 InstructionIndex = 0; InstructionIndex < InstructionCount; InstructionIndex++)
	{
		FRigVMInstruction Instruction;
		ERigVMOpCode OpCode = ERigVMOpCode::Invalid;

		if (Ar.IsSaving())
		{
			Instruction = Instructions[InstructionIndex];
			OpCode = Instruction.OpCode;
		}
			
		Ar << OpCode;

		switch (OpCode)
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
				if (Ar.IsSaving())
				{
					FRigVMExecuteOp Op = GetOpAt<FRigVMExecuteOp>(Instruction.ByteCodeIndex);
					Ar << Op;

					FRigVMOperandArray Operands = GetOperandsForExecuteOp(Instruction);
					int32 OperandCount = (int32)Op.GetOperandCount();
					ensure(OperandCount == Operands.Num());

					for (int32 OperandIndex = 0; OperandIndex < OperandCount; OperandIndex++)
					{
						FRigVMOperand Operand = Operands[OperandIndex];
						Ar << Operand;
					}
				}
				else
				{
					FRigVMExecuteOp Op;
					Ar << Op;

					int32 OperandCount = Op.GetOperandCount();
					TArray<FRigVMOperand> Operands;
					for (int32 OperandIndex = 0; OperandIndex < OperandCount; OperandIndex++)
					{
						FRigVMOperand Operand;
						Ar << Operand;
						Operands.Add(Operand);
					}

					AddExecuteOp(Op.FunctionIndex, Operands);
				}
				break;
			}
			case ERigVMOpCode::Copy:
			{
				if (Ar.IsSaving())
				{
					FRigVMCopyOp Op = GetOpAt<FRigVMCopyOp>(Instruction.ByteCodeIndex);
					Ar << Op;
				}
				else
				{
					FRigVMCopyOp Op;
					Ar << Op;
					AddOp<FRigVMCopyOp>(Op);
				}
				break;
			}
			case ERigVMOpCode::Zero:
			case ERigVMOpCode::BoolFalse:
			case ERigVMOpCode::BoolTrue:
			case ERigVMOpCode::Increment:
			case ERigVMOpCode::Decrement:
			{
				if (Ar.IsSaving())
				{
					FRigVMUnaryOp Op = GetOpAt<FRigVMUnaryOp>(Instruction.ByteCodeIndex);
					Ar << Op;
				}
				else
				{
					FRigVMUnaryOp Op;
					Ar << Op;
					AddOp<FRigVMUnaryOp>(Op);
				}
				break;
			}
			case ERigVMOpCode::Equals:
			case ERigVMOpCode::NotEquals:
			{
				if (Ar.IsSaving())
				{
					FRigVMComparisonOp Op = GetOpAt<FRigVMComparisonOp>(Instruction.ByteCodeIndex);
					Ar << Op;
				}
				else
				{
					FRigVMComparisonOp Op;
					Ar << Op;
					AddOp<FRigVMComparisonOp>(Op);
				}
				break;
			}
			case ERigVMOpCode::JumpAbsolute:
			case ERigVMOpCode::JumpForward:
			case ERigVMOpCode::JumpBackward:
			{
				if (Ar.IsSaving())
				{
					FRigVMJumpOp Op = GetOpAt<FRigVMJumpOp>(Instruction.ByteCodeIndex);
					Ar << Op;
				}
				else
				{
					FRigVMJumpOp Op;
					Ar << Op;
					AddOp<FRigVMJumpOp>(Op);
				}
				break;
			}
			case ERigVMOpCode::JumpAbsoluteIf:
			case ERigVMOpCode::JumpForwardIf:
			case ERigVMOpCode::JumpBackwardIf:
			{
				if (Ar.IsSaving())
				{
					FRigVMJumpIfOp Op = GetOpAt<FRigVMJumpIfOp>(Instruction.ByteCodeIndex);
					Ar << Op;
				}
				else
				{
					FRigVMJumpIfOp Op;
					Ar << Op;
					AddOp<FRigVMJumpIfOp>(Op);
				}
				break;
			}
			case ERigVMOpCode::Exit:
			{
				if (Ar.IsSaving())
				{
					// nothing todo, the ExitOp has no custom data inside of it
					// so all we need is the previously saved OpCode.
				}
				else
				{
					AddExitOp();
				}
				break;
			}
			case ERigVMOpCode::BeginBlock:
			{
				if (Ar.IsSaving())
				{
					FRigVMBinaryOp Op = GetOpAt<FRigVMBinaryOp>(Instruction.ByteCodeIndex);
					Ar << Op;
				}
				else
				{
					FRigVMBinaryOp Op;
					Ar << Op;
					AddOp<FRigVMBinaryOp>(Op);
				}
				break;
			}
			case ERigVMOpCode::EndBlock:
			{
				if (Ar.IsSaving())
				{
					// nothing todo, the EndBlock has no custom data inside of it
					// so all we need is the previously saved OpCode.
				}
				else
				{
					AddEndBlockOp();
				}
				break;
			}
			default:
			{
				ensure(false);
			}
		}
	}

	if (Ar.IsLoading())
	{
		AlignByteCode();

		Entries.Reset();
		if (Ar.CustomVer(FAnimObjectVersion::GUID) >= FAnimObjectVersion::SerializeRigVMEntries)
		{
			UScriptStruct* ScriptStruct = FRigVMByteCodeEntry::StaticStruct();

			TArray<FString> View;
			Ar << View;

			for (int32 EntryIndex = 0; EntryIndex < View.Num(); EntryIndex++)
			{
				FRigVMByteCodeEntry Entry;
				ScriptStruct->ImportText(*View[EntryIndex], &Entry, nullptr, PPF_None, nullptr, ScriptStruct->GetName());
				Entries.Add(Entry);
			}
		}
	}
	else if(Ar.IsSaving())
	{
		UScriptStruct* ScriptStruct = FRigVMByteCodeEntry::StaticStruct();
		TArray<uint8, TAlignedHeapAllocator<16>> DefaultStructData;
		DefaultStructData.AddZeroed(ScriptStruct->GetStructureSize());
		ScriptStruct->InitializeDefaultValue(DefaultStructData.GetData());

		TArray<FString> View;
		for (uint16 EntryIndex = 0; EntryIndex < Entries.Num(); EntryIndex++)
		{
			FString Value;
			ScriptStruct->ExportText(Value, &Entries[EntryIndex], DefaultStructData.GetData(), nullptr, PPF_None, nullptr);
			View.Add(Value);
		}

		Ar << View;
	}

	return true;
}

void FRigVMByteCode::Reset()
{
	ByteCode.Reset();
	bByteCodeIsAligned = false;
	NumInstructions = 0;
	Entries.Reset();
}

void FRigVMByteCode::Empty()
{
	ByteCode.Empty();
	bByteCodeIsAligned = false;
	NumInstructions = 0;
	Entries.Empty();
}

uint64 FRigVMByteCode::Num() const
{
	return (uint64)ByteCode.Num();
}

uint64 FRigVMByteCode::NumEntries() const
{
	return Entries.Num();
}

const FRigVMByteCodeEntry& FRigVMByteCode::GetEntry(int32 InEntryIndex) const
{
	return Entries[InEntryIndex];
}

int32 FRigVMByteCode::FindEntryIndex(const FName& InEntryName) const
{
	for (int32 EntryIndex = 0; EntryIndex < Entries.Num(); EntryIndex++)
	{
		if (Entries[EntryIndex].Name == InEntryName)
		{
			return EntryIndex;
		}
	}
	return INDEX_NONE;
}

uint64 FRigVMByteCode::GetOpNumBytesAt(uint64 InByteCodeIndex, bool bIncludeOperands) const
{
	ERigVMOpCode OpCode = GetOpCodeAt(InByteCodeIndex);
	switch (OpCode)
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
			uint64 NumBytes = (uint64)sizeof(FRigVMExecuteOp);
			if(bIncludeOperands)
			{
				FRigVMExecuteOp ExecuteOp;
				uint8* ExecuteOpPtr = (uint8*)&ExecuteOp;
				for (int32 ByteIndex = 0; ByteIndex < sizeof(FRigVMExecuteOp); ByteIndex++)
				{
					ExecuteOpPtr[ByteIndex] = ByteCode[InByteCodeIndex + ByteIndex];
				}

				if (bByteCodeIsAligned)
				{
					static const uint64 OperandAlignment = GetOperandAlignment();
					if (OperandAlignment > 0)
					{
						while (!IsAligned(&ByteCode[InByteCodeIndex + NumBytes], OperandAlignment))
						{
							NumBytes++;
						}
					}
				}
				NumBytes += (uint64)ExecuteOp.GetOperandCount() * (uint64)sizeof(FRigVMOperand);
			}
			return NumBytes;
		}
		case ERigVMOpCode::Copy:
		{
			return (uint64)sizeof(FRigVMCopyOp);
		}
		case ERigVMOpCode::Zero:
		case ERigVMOpCode::BoolFalse:
		case ERigVMOpCode::BoolTrue:
		case ERigVMOpCode::Increment:
		case ERigVMOpCode::Decrement:
		{
			return (uint64)sizeof(FRigVMUnaryOp);
		}
		case ERigVMOpCode::Equals:
		case ERigVMOpCode::NotEquals:
		{
			return (uint64)sizeof(FRigVMComparisonOp);
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
			return (uint64)sizeof(FRigVMBaseOp);
		}
		case ERigVMOpCode::BeginBlock:
		{
			return (uint64)sizeof(FRigVMBinaryOp);
		}
		case ERigVMOpCode::EndBlock:
		{
			return (uint64)sizeof(FRigVMBaseOp);
		}
		case ERigVMOpCode::Invalid:
		{
			ensure(false);
			return 0;
		}
	}
	return 0;
}

uint64 FRigVMByteCode::AddZeroOp(const FRigVMOperand& InArg)
{
	FRigVMUnaryOp Op(ERigVMOpCode::Zero, InArg);
	return AddOp(Op);
}

uint64 FRigVMByteCode::AddFalseOp(const FRigVMOperand& InArg)
{
	FRigVMUnaryOp Op(ERigVMOpCode::BoolFalse, InArg);
	return AddOp(Op);
}

uint64 FRigVMByteCode::AddTrueOp(const FRigVMOperand& InArg)
{
	FRigVMUnaryOp Op(ERigVMOpCode::BoolTrue, InArg);
	return AddOp(Op);
}

uint64 FRigVMByteCode::AddCopyOp(const FRigVMOperand& InSource, const FRigVMOperand& InTarget)
{
	ensure(InTarget.GetMemoryType() != ERigVMMemoryType::Literal);
	FRigVMCopyOp Op(InSource, InTarget);
	return AddOp(Op);
}

uint64 FRigVMByteCode::AddIncrementOp(const FRigVMOperand& InArg)
{
	ensure(InArg.GetMemoryType() != ERigVMMemoryType::Literal);
	FRigVMUnaryOp Op(ERigVMOpCode::Increment, InArg);
	return AddOp(Op);
}

uint64 FRigVMByteCode::AddDecrementOp(const FRigVMOperand& InArg)
{
	ensure(InArg.GetMemoryType() != ERigVMMemoryType::Literal);
	FRigVMUnaryOp Op(ERigVMOpCode::Decrement, InArg);
	return AddOp(Op);
}

uint64 FRigVMByteCode::AddEqualsOp(const FRigVMOperand& InA, const FRigVMOperand& InB, const FRigVMOperand& InResult)
{
	FRigVMComparisonOp Op(ERigVMOpCode::Equals, InA, InB, InResult);
	return AddOp(Op);
}

uint64 FRigVMByteCode::AddNotEqualsOp(const FRigVMOperand& InA, const FRigVMOperand& InB, const FRigVMOperand& InResult)
{
	FRigVMComparisonOp Op(ERigVMOpCode::NotEquals, InA, InB, InResult);
	return AddOp(Op);
}

uint64 FRigVMByteCode::AddJumpOp(ERigVMOpCode InOpCode, uint16 InInstructionIndex)
{
	FRigVMJumpOp Op(InOpCode, InInstructionIndex);
	return AddOp(Op);
}

uint64 FRigVMByteCode::AddJumpIfOp(ERigVMOpCode InOpCode, uint16 InInstructionIndex, const FRigVMOperand& InConditionArg, bool bJumpWhenConditionIs)
{
	FRigVMJumpIfOp Op(InOpCode, InConditionArg, InInstructionIndex, bJumpWhenConditionIs);
	return AddOp(Op);
}

uint64 FRigVMByteCode::AddChangeTypeOp(FRigVMOperand InArg, ERigVMRegisterType InType, uint16 InElementSize, uint16 InElementCount, uint16 InSliceCount)
{
	FRigVMChangeTypeOp Op(InArg, InType, InElementSize, InElementCount, InSliceCount);
	return AddOp(Op);
}

uint64 FRigVMByteCode::AddExecuteOp(uint16 InFunctionIndex, const FRigVMOperandArray& InOperands)
{
	FRigVMExecuteOp Op(InFunctionIndex, (uint8)InOperands.Num());
	uint64 OpByteIndex = AddOp(Op);

	uint64 OperandsByteIndex = (uint64)ByteCode.AddZeroed(sizeof(FRigVMOperand) * InOperands.Num());
	FMemory::Memcpy(ByteCode.GetData() + OperandsByteIndex, InOperands.GetData(), sizeof(FRigVMOperand) * InOperands.Num());
	return OpByteIndex;
}

uint64 FRigVMByteCode::AddExitOp()
{
	FRigVMBaseOp Op(ERigVMOpCode::Exit);
	return AddOp(Op);
}

FString FRigVMByteCode::DumpToText() const
{
	TArray<FString> Lines;

	FRigVMInstructionArray Instructions = GetInstructions();
	for (int32 InstructionIndex = 0; InstructionIndex < Instructions.Num(); InstructionIndex++)
	{
		const FRigVMInstruction& Instruction = Instructions[InstructionIndex];
		ERigVMOpCode OpCode = Instruction.OpCode;

		FString Line = StaticEnum<ERigVMOpCode>()->GetNameByValue((int64)OpCode).ToString();

		switch (OpCode)
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
				const FRigVMExecuteOp& Op = GetOpAt<FRigVMExecuteOp>(Instruction.ByteCodeIndex);
				Line += FString::Printf(TEXT(", FunctionIndex %d"), (int32)Op.FunctionIndex);

				FRigVMOperandArray Operands = GetOperandsForExecuteOp(Instruction);
				if (Operands.Num() > 0)
				{
					TArray<FString> OperandsContent;
					for (const FRigVMOperand& Operand : Operands)
					{
						FString OperandContent;
						FRigVMOperand::StaticStruct()->ExportText(OperandContent, &Operand, nullptr, nullptr, PPF_None, nullptr);
						OperandsContent.Add(FString::Printf(TEXT("\t%s"), *OperandContent));
					}

					Line += FString::Printf(TEXT("(\n%s\n)"), *FString::Join(OperandsContent, TEXT("\n")));
				}
				break;
			}
			case ERigVMOpCode::Copy:
			{
				const FRigVMCopyOp& Op = GetOpAt<FRigVMCopyOp>(Instruction.ByteCodeIndex);
				FString SourceContent;
				FRigVMOperand::StaticStruct()->ExportText(SourceContent, &Op.Source, nullptr, nullptr, PPF_None, nullptr);
				Line += FString::Printf(TEXT(", Source %s"), *SourceContent);
				FString TargetContent;
				FRigVMOperand::StaticStruct()->ExportText(TargetContent, &Op.Source, nullptr, nullptr, PPF_None, nullptr);
				Line += FString::Printf(TEXT(", Target %s"), *TargetContent);
				break;
			}
			case ERigVMOpCode::Zero:
			case ERigVMOpCode::BoolFalse:
			case ERigVMOpCode::BoolTrue:
			case ERigVMOpCode::Increment:
			case ERigVMOpCode::Decrement:
			{
				const FRigVMUnaryOp& Op = GetOpAt<FRigVMUnaryOp>(Instruction.ByteCodeIndex);
				FString ArgContent;
				FRigVMOperand::StaticStruct()->ExportText(ArgContent, &Op.Arg, nullptr, nullptr, PPF_None, nullptr);
				Line += FString::Printf(TEXT(", Source %s"), *ArgContent);
				break;
			}
			case ERigVMOpCode::Equals:
			case ERigVMOpCode::NotEquals:
			{
				const FRigVMComparisonOp& Op = GetOpAt<FRigVMComparisonOp>(Instruction.ByteCodeIndex);
				FString AContent;
				FRigVMOperand::StaticStruct()->ExportText(AContent, &Op.A, nullptr, nullptr, PPF_None, nullptr);
				Line += FString::Printf(TEXT(", A %s"), *AContent);
				FString BContent;
				FRigVMOperand::StaticStruct()->ExportText(BContent, &Op.B, nullptr, nullptr, PPF_None, nullptr);
				Line += FString::Printf(TEXT(", B %s"), *BContent);
				FString ResultContent;
				FRigVMOperand::StaticStruct()->ExportText(ResultContent, &Op.Result, nullptr, nullptr, PPF_None, nullptr);
				Line += FString::Printf(TEXT(", Result %s"), *ResultContent);
				break;
			}
			case ERigVMOpCode::JumpAbsolute:
			case ERigVMOpCode::JumpForward:
			case ERigVMOpCode::JumpBackward:
			{
				const FRigVMJumpOp& Op = GetOpAt<FRigVMJumpOp>(Instruction.ByteCodeIndex);
				Line += FString::Printf(TEXT(", InstructionIndex %d"), (int32)Op.InstructionIndex);
				break;
			}
			case ERigVMOpCode::JumpAbsoluteIf:
			case ERigVMOpCode::JumpForwardIf:
			case ERigVMOpCode::JumpBackwardIf:
			{
				const FRigVMJumpIfOp& Op = GetOpAt<FRigVMJumpIfOp>(Instruction.ByteCodeIndex);
				Line += FString::Printf(TEXT(", InstructionIndex %d"), (int32)Op.InstructionIndex);
				FString ArgContent;
				FRigVMOperand::StaticStruct()->ExportText(ArgContent, &Op.Arg, nullptr, nullptr, PPF_None, nullptr);
				Line += FString::Printf(TEXT(", Source %s"), *ArgContent);
				Line += FString::Printf(TEXT(", Condition %d"), (int32)(Op.Condition ? 0 : 1));
				break;
			}
			case ERigVMOpCode::Exit:
			{
				break;
			}
			case ERigVMOpCode::Invalid:
			{
				ensure(false);
			}
		}

		Lines.Add(Line);
	}

	if (Lines.Num() == 0)
	{
		return FString();
	}

	return FString::Join(Lines, TEXT("\n"));
}

uint64 FRigVMByteCode::AddBeginBlockOp(FRigVMOperand InCountArg, FRigVMOperand InIndexArg)
{
	FRigVMBinaryOp Op(ERigVMOpCode::BeginBlock, InCountArg, InIndexArg);
	return AddOp(Op);
}

uint64 FRigVMByteCode::AddEndBlockOp()
{
	FRigVMBaseOp Op(ERigVMOpCode::EndBlock);
	return AddOp(Op);
}


uint64 FRigVMByteCode::GetOpAlignment(ERigVMOpCode InOpCode) const
{
	switch (InOpCode)
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
			static const uint64 Alignment = FRigVMExecuteOp::StaticStruct()->GetCppStructOps()->GetAlignment();
			return Alignment;
		}
		case ERigVMOpCode::Copy:
		{
			static const uint64 Alignment = FRigVMCopyOp::StaticStruct()->GetCppStructOps()->GetAlignment();
			return Alignment;
		}
		case ERigVMOpCode::Zero:
		case ERigVMOpCode::BoolFalse:
		case ERigVMOpCode::BoolTrue:
		case ERigVMOpCode::Increment:
		case ERigVMOpCode::Decrement:
		{
			static const uint64 Alignment = FRigVMUnaryOp::StaticStruct()->GetCppStructOps()->GetAlignment();
			return Alignment;
		}
		case ERigVMOpCode::Equals:
		case ERigVMOpCode::NotEquals:
		{
			static const uint64 Alignment = FRigVMComparisonOp::StaticStruct()->GetCppStructOps()->GetAlignment();
			return Alignment;
		}
		case ERigVMOpCode::JumpAbsolute:
		case ERigVMOpCode::JumpForward:
		case ERigVMOpCode::JumpBackward:
		{
			static const uint64 Alignment = FRigVMJumpOp::StaticStruct()->GetCppStructOps()->GetAlignment();
			return Alignment;
		}
		case ERigVMOpCode::JumpAbsoluteIf:
		case ERigVMOpCode::JumpForwardIf:
		case ERigVMOpCode::JumpBackwardIf:
		{
			static const uint64 Alignment = FRigVMJumpIfOp::StaticStruct()->GetCppStructOps()->GetAlignment();
			return Alignment;
		}
		case ERigVMOpCode::ChangeType:
		{
			static const uint64 Alignment = FRigVMChangeTypeOp::StaticStruct()->GetCppStructOps()->GetAlignment();
			return Alignment;
		}
		case ERigVMOpCode::Exit:
		{
			static const uint64 Alignment = FRigVMBaseOp::StaticStruct()->GetCppStructOps()->GetAlignment();
			return Alignment;
		}
		case ERigVMOpCode::BeginBlock:
		{
			static const uint64 Alignment = FRigVMBinaryOp::StaticStruct()->GetCppStructOps()->GetAlignment();
			return Alignment;
		}
		case ERigVMOpCode::EndBlock:
		{
			static const uint64 Alignment = FRigVMBaseOp::StaticStruct()->GetCppStructOps()->GetAlignment();
			return Alignment;
		}
		case ERigVMOpCode::Invalid:
		{
			ensure(false);
			return 0;
		}
	}
	return 0;
}

uint64 FRigVMByteCode::GetOperandAlignment() const
{
	static const uint64 OperandAlignment = !FRigVMOperand::StaticStruct()->GetCppStructOps()->GetAlignment();
	return OperandAlignment;
}

void FRigVMByteCode::AlignByteCode()
{
	if (bByteCodeIsAligned)
	{
		return;
	}

	if (ByteCode.Num() == 0)
	{
		return;
	}

	FRigVMInstructionArray Instructions(*this, false);
	uint64 BytesToReserve = ByteCode.Num();

	for (int32 InstructionIndex = 0; InstructionIndex < Instructions.Num(); InstructionIndex++)
	{
		const FRigVMInstruction& Instruction = Instructions[InstructionIndex];
		BytesToReserve += GetOpAlignment(Instruction.OpCode);

		if (Instruction.OpCode >= ERigVMOpCode::Execute_0_Operands && Instruction.OpCode < ERigVMOpCode::Execute_64_Operands)
		{
			BytesToReserve += GetOperandAlignment();
		}
	}

	TArray<uint8> AlignedByteCode;
	AlignedByteCode.Reserve(BytesToReserve);
	AlignedByteCode.AddZeroed(ByteCode.Num());

	uint64 ShiftedBytes = 0;
	for (int32 InstructionIndex = 0; InstructionIndex < Instructions.Num(); InstructionIndex++)
	{
		const FRigVMInstruction& Instruction = Instructions[InstructionIndex];
		uint64 OriginalByteCodeIndex = Instruction.ByteCodeIndex;
		uint64 AlignedByteCodeIndex = OriginalByteCodeIndex + ShiftedBytes;
		uint64 OpAlignment = GetOpAlignment(Instruction.OpCode);

		if (OpAlignment > 0)
		{
			while (!IsAligned(&AlignedByteCode[AlignedByteCodeIndex], OpAlignment))
			{
				AlignedByteCode[AlignedByteCodeIndex] = (uint8)Instruction.OpCode;
				AlignedByteCodeIndex++;
				ShiftedBytes++;
				AlignedByteCode.AddZeroed(1);
			}
		}

		uint64 NumBytesToCopy = GetOpNumBytesAt(OriginalByteCodeIndex, false);
		for (int32 ByteIndex = 0; ByteIndex < NumBytesToCopy; ByteIndex++)
		{
			AlignedByteCode[AlignedByteCodeIndex + ByteIndex] = ByteCode[OriginalByteCodeIndex + ByteIndex];
		}

		if (Instruction.OpCode >= ERigVMOpCode::Execute_0_Operands && Instruction.OpCode < ERigVMOpCode::Execute_64_Operands)
		{
			AlignedByteCodeIndex += NumBytesToCopy;

			static const uint64 OperandAlignment = GetOperandAlignment();
			if (OperandAlignment > 0)
			{
				while (!IsAligned(&AlignedByteCode[AlignedByteCodeIndex], OperandAlignment))
				{
					AlignedByteCodeIndex++;
					ShiftedBytes++;
					AlignedByteCode.AddZeroed(1);
				}
			}

			FRigVMExecuteOp ExecuteOp;
			uint8* ExecuteOpPtr = (uint8*)&ExecuteOp;
			for (int32 ByteIndex = 0; ByteIndex < sizeof(FRigVMExecuteOp); ByteIndex++)
			{
				ExecuteOpPtr[ByteIndex] = ByteCode[OriginalByteCodeIndex + ByteIndex];
			}

			OriginalByteCodeIndex += NumBytesToCopy;
			NumBytesToCopy = sizeof(FRigVMOperand) * ExecuteOp.GetOperandCount();

			for (int32 ByteIndex = 0; ByteIndex < NumBytesToCopy; ByteIndex++)
			{
				AlignedByteCode[AlignedByteCodeIndex + ByteIndex] = ByteCode[OriginalByteCodeIndex + ByteIndex];
			}
		}
	}

	Swap(ByteCode, AlignedByteCode);
	bByteCodeIsAligned = true;
}