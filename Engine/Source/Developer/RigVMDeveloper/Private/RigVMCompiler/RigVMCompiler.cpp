// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCompiler/RigVMCompiler.h"
#include "RigVMModel/RigVMController.h"
#include "RigVMCore/RigVMExecuteContext.h"
#include "RigVMDeveloperModule.h"
#include "UObject/PropertyPortFlags.h"
#include "Stats/StatsHierarchical.h"

class FRigVMCompilerImportErrorContext : public FOutputDevice
{
public:

	URigVMCompiler* Compiler;
	int32 NumErrors;

	FRigVMCompilerImportErrorContext(URigVMCompiler* InCompiler)
		: FOutputDevice()
		, Compiler(InCompiler)
		, NumErrors(0)
	{
	}

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category) override
	{
		switch (Verbosity)
		{
		case ELogVerbosity::Error:
		case ELogVerbosity::Fatal:
		{
			Compiler->ReportError(V);
			break;
		}
		case ELogVerbosity::Warning:
		{
			Compiler->ReportWarning(V);
			break;
		}
		default:
		{
			Compiler->ReportInfo(V);
			break;
		}
		}
		NumErrors++;
	}
};

FRigVMCompileSettings::FRigVMCompileSettings()
	: SurpressInfoMessages(true)
	, SurpressWarnings(false)
	, SurpressErrors(false)
	, EnablePinWatches(true)
	, SplitLiteralsFromWorkMemory(true)
	, ConsolidateWorkRegisters(true)
	, ASTSettings(FRigVMParserASTSettings::Optimized())
{
}

int32 FRigVMCompilerWorkData::IncRefRegister(int32 InRegister, int32 InIncrement)
{
	ensure(InIncrement > 0);
	if (int32* RefCountPtr = RegisterRefCount.Find(InRegister))
	{
		*RefCountPtr += InIncrement;
		return *RefCountPtr;
	}
	RegisterRefCount.Add(InRegister, InIncrement);
	return InIncrement;
}

int32 FRigVMCompilerWorkData::DecRefRegister(int32 InRegister, int32 InDecrement)
{
	ensure(InDecrement > 0);
	if (int32* RefCountPtr = RegisterRefCount.Find(InRegister))
	{
		int32& RefCount = *RefCountPtr;
		RefCount = FMath::Max<int32>(0, RefCount - InDecrement);
		return RefCount;
	}
	return 0;
}

URigVMCompiler::URigVMCompiler()
{
}

bool URigVMCompiler::Compile(URigVMGraph* InGraph, URigVM* OutVM, const FRigVMUserDataArray& InRigVMUserData, TMap<FString, FRigVMOperand>* OutOperands)
{
	if (InGraph == nullptr)
	{
		ReportError(TEXT("Provided graph is nullptr."));
		return false;
	}
	if (OutVM == nullptr)
	{
		ReportError(TEXT("Provided vm is nullptr."));
		return false;
	}

	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	OutVM->Reset();

	for (URigVMNode* Node : InGraph->Nodes)
	{
		Node->InstructionIndex = INDEX_NONE;
	}

	TMap<FString, FRigVMOperand> LocalOperands;
	if (OutOperands == nullptr)
	{
		OutOperands = &LocalOperands;
	}
	OutOperands->Reset();

	TSharedPtr<FRigVMParserAST> AST = InGraph->GetRuntimeAST(Settings.ASTSettings, true /* force */);
	ensure(AST.IsValid());

	FRigVMCompilerWorkData WorkData;
	WorkData.VM = OutVM;
	WorkData.PinPathToOperand = OutOperands;
	WorkData.RigVMUserData = InRigVMUserData;
	WorkData.bSetupMemory = true;
	WorkData.NumInstructions = 0;

	// define all parameters independent from sorted nodes
	for (const FRigVMExprAST* Expr : AST->Expressions)
	{
		if (Expr->IsA(FRigVMExprAST::EType::Var))
		{
			const FRigVMVarExprAST* VarExpr = Expr->To<FRigVMVarExprAST>();
			{
				if(Cast<URigVMParameterNode>(VarExpr->GetPin()->GetNode()))
				{
					if (VarExpr->GetPin()->GetName() == TEXT("Value"))
					{
						FindOrAddRegister(VarExpr, WorkData, false /* watchvalue */);
					}
				}
			}
		}
	}

	WorkData.ExprComplete.Reset();
	for (FRigVMExprAST* RootExpr : *AST)
	{
		TraverseExpression(RootExpr, WorkData);
	}

	// If a parameter node has no corresponding AST node, because it's not on the execution path,
	// then add a dummy parameter with no register index, so that we can properly add the pins on the 
	// ControlRig node later and not lose any existing external connections.
	for (URigVMNode* Node : InGraph->GetNodes())
	{
		if (URigVMParameterNode* ParameterNode = Cast<URigVMParameterNode>(Node))
		{
			const FRigVMExprAST* ParameterExpr = AST->GetExprForSubject(ParameterNode);
			check(ParameterExpr);

			if (!ParameterExpr || ParameterExpr->IsA(FRigVMExprAST::NoOp))
			{
				FName Name = ParameterNode->GetParameterName();

				if (!WorkData.VM->ParametersNameMap.Contains(Name))
				{
					ERigVMParameterType ParameterType = ParameterNode->IsInput() ? ERigVMParameterType::Input : ERigVMParameterType::Output;
					FRigVMParameter Parameter(ParameterType, Name, INDEX_NONE, ParameterNode->GetCPPType(), nullptr);
					WorkData.VM->ParametersNameMap.Add(Parameter.Name, WorkData.VM->Parameters.Add(Parameter));
				}
			}
		}
	}

	WorkData.bSetupMemory = false;
	WorkData.ExprComplete.Reset();
	for (FRigVMExprAST* RootExpr : *AST)
	{
		TraverseExpression(RootExpr, WorkData);
	}

	if (WorkData.VM->ByteCode.GetInstructions().Num() == 0)
	{
		WorkData.VM->ByteCode.AddExitOp();
		WorkData.NumInstructions++;
	}

	return true;
}

void URigVMCompiler::TraverseExpression(const FRigVMExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	if (WorkData.ExprComplete.Contains(InExpr))
	{
		return;
	}
	WorkData.ExprComplete.Add(InExpr, true);

	switch (InExpr->GetType())
	{
		case FRigVMExprAST::EType::Block:
		{
			TraverseBlock(InExpr->To<FRigVMBlockExprAST>(), WorkData);
			break;
		}
		case FRigVMExprAST::EType::Entry:
		{
			TraverseEntry(InExpr->To<FRigVMEntryExprAST>(), WorkData);
			break;
		}
		case FRigVMExprAST::EType::CallExtern:
		{
			TraverseCallExtern(InExpr->To<FRigVMCallExternExprAST>(), WorkData);
			break;
		}
		case FRigVMExprAST::EType::NoOp:
		{
			TraverseNoOp(InExpr->To<FRigVMNoOpExprAST>(), WorkData);
			break;
		}
		case FRigVMExprAST::EType::Var:
		{
			TraverseVar(InExpr->To<FRigVMVarExprAST>(), WorkData);
			break;
		}
		case FRigVMExprAST::EType::Literal:
		{
			TraverseLiteral(InExpr->To<FRigVMLiteralExprAST>(), WorkData);
			break;
		}
		case FRigVMExprAST::EType::Assign:
		{
			TraverseAssign(InExpr->To<FRigVMAssignExprAST>(), WorkData);
			break;
		}
		case FRigVMExprAST::EType::Copy:
		{
			TraverseCopy(InExpr->To<FRigVMCopyExprAST>(), WorkData);
			break;
		}
		case FRigVMExprAST::EType::CachedValue:
		{
			TraverseCachedValue(InExpr->To<FRigVMCachedValueExprAST>(), WorkData);
			break;
		}
		case FRigVMExprAST::EType::Exit:
		{
			TraverseExit(InExpr->To<FRigVMExitExprAST>(), WorkData);
			break;
		}
		default:
		{
			ensure(false);
			break;
		}
	}
}

void URigVMCompiler::TraverseChildren(const FRigVMExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	for (FRigVMExprAST* ChildExpr : *InExpr)
	{
		TraverseExpression(ChildExpr, WorkData);
	}
}

void URigVMCompiler::TraverseBlock(const FRigVMBlockExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	// root blocks are used to store unused expressions
	if (InExpr->GetParent() == nullptr)
	{
		return;
	}
	TraverseChildren(InExpr, WorkData);
}

void URigVMCompiler::TraverseEntry(const FRigVMEntryExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	URigVMStructNode* StructNode = Cast<URigVMStructNode>(InExpr->GetNode());

	if (WorkData.bSetupMemory)
	{
		TArray<uint8, TAlignedHeapAllocator<16>> TempBuffer;
		FRigVMStruct* DefaultStruct = nullptr;
		if (UScriptStruct* Struct = StructNode->GetScriptStruct())
		{
			TempBuffer.AddUninitialized(Struct->GetStructureSize());
			Struct->InitializeDefaultValue(TempBuffer.GetData());
			DefaultStruct = (FRigVMStruct*)TempBuffer.GetData();
		}

		WorkData.DefaultStructs.Add(DefaultStruct);
		TraverseChildren(InExpr, WorkData);
		WorkData.DefaultStructs.Pop();

		if (UScriptStruct* Struct = StructNode->GetScriptStruct())
		{
			Struct->DestroyStruct(TempBuffer.GetData(), 1);
		}
	}
	else
	{
		// todo: define the entry in the VM
		TArray<FRigVMOperand> Operands;
		for (FRigVMExprAST* ChildExpr : *InExpr)
		{
			if (ChildExpr->IsA(FRigVMExprAST::EType::Var))
			{
				Operands.Add(WorkData.ExprToOperand.FindChecked(ChildExpr->To<FRigVMVarExprAST>()));
			}
			else
			{
				break;
			}
		}

		// setup the instruction
		int32 FunctionIndex = WorkData.VM->AddRigVMFunction(StructNode->GetScriptStruct(), StructNode->GetMethodName());
		WorkData.VM->ByteCode.AddExecuteOp(FunctionIndex, Operands);
		StructNode->InstructionIndex = WorkData.NumInstructions++;

		TraverseChildren(InExpr, WorkData);
	}
}

void URigVMCompiler::TraverseCallExtern(const FRigVMCallExternExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	URigVMStructNode* StructNode = Cast<URigVMStructNode>(InExpr->GetNode());

	if (WorkData.bSetupMemory)
	{
		TArray<uint8, TAlignedHeapAllocator<16>> TempBuffer;
		FRigVMStruct* DefaultStruct = nullptr;
		if (UScriptStruct* Struct = StructNode->GetScriptStruct())
		{
			TempBuffer.AddUninitialized(Struct->GetStructureSize());
			Struct->InitializeDefaultValue(TempBuffer.GetData());
			DefaultStruct = (FRigVMStruct*)TempBuffer.GetData();
		}

		WorkData.DefaultStructs.Add(DefaultStruct);
		TraverseChildren(InExpr, WorkData);
		WorkData.DefaultStructs.Pop();

		if (Settings.EnablePinWatches)
		{
			for (URigVMPin* Pin : StructNode->Pins)
			{
				if (Pin->RequiresWatch())
				{
					FRigVMVarExprAST TempVarExpr(nullptr, Pin);
					FindOrAddRegister(&TempVarExpr, WorkData, true);
				}
			}
		}

		if (Settings.ConsolidateWorkRegisters)
		{
			for (const FRigVMExprAST* Child : *InExpr)
			{
				const FRigVMExprAST* VarExpr = Child;
				if (VarExpr->IsA(FRigVMExprAST::EType::CachedValue))
				{
					VarExpr = VarExpr->To<FRigVMCachedValueExprAST>()->GetVarExpr();
				}
				if (VarExpr->IsA(FRigVMExprAST::EType::Var))
				{
					if (const FRigVMOperand* Operand = WorkData.ExprToOperand.Find(VarExpr->To<FRigVMVarExprAST>()))
					{
						if (Operand->GetMemoryType() != ERigVMMemoryType::Literal)
						{
							WorkData.DecRefRegister(Operand->GetRegisterIndex());
						}
					}
				}
			}
		}

		if (UScriptStruct* Struct = StructNode->GetScriptStruct())
		{
			Struct->DestroyStruct(TempBuffer.GetData(), 1);
		}
	}
	else
	{
		TArray<FRigVMOperand> Operands;
		for (FRigVMExprAST* ChildExpr : *InExpr)
		{
			if (ChildExpr->GetType() == FRigVMExprAST::EType::CachedValue)
			{
				Operands.Add(WorkData.ExprToOperand.FindChecked(ChildExpr->To<FRigVMCachedValueExprAST>()->GetVarExpr()));
			}
			else if (ChildExpr->IsA(FRigVMExprAST::EType::Var))
			{
				Operands.Add(WorkData.ExprToOperand.FindChecked(ChildExpr->To<FRigVMVarExprAST>()));
			}
			else
			{
				break;
			}
		}

		TraverseChildren(InExpr, WorkData);

		// setup the instruction
		int32 FunctionIndex = WorkData.VM->AddRigVMFunction(StructNode->GetScriptStruct(), StructNode->GetMethodName());
		WorkData.VM->ByteCode.AddExecuteOp(FunctionIndex, Operands);
		StructNode->InstructionIndex = WorkData.NumInstructions++;

		ensure(InExpr->NumChildren() == StructNode->Pins.Num());
		for (int32 PinIndex = 0; PinIndex < StructNode->Pins.Num(); PinIndex++)
		{
			URigVMPin* Pin = StructNode->Pins[PinIndex];

			// ensure to copy the debug values
			if (Pin->RequiresWatch() && Settings.EnablePinWatches)
			{
				const FRigVMExprAST* PinExpr = InExpr->ChildAt(PinIndex);
				if (PinExpr->IsA(FRigVMExprAST::EType::CachedValue))
				{
					PinExpr = PinExpr->To<FRigVMCachedValueExprAST>()->GetVarExpr();
				}
				ensure(PinExpr->IsA(FRigVMExprAST::EType::Var));
				FRigVMOperand Source = WorkData.ExprToOperand.FindChecked(PinExpr->To<FRigVMVarExprAST>());
				FString PinHash = GetPinHash(Pin, PinExpr->To<FRigVMVarExprAST>(), true);
				if (const FRigVMOperand* Target = WorkData.PinPathToOperand->Find(PinHash))
				{
					WorkData.VM->ByteCode.AddCopyOp(Source, *Target);
					WorkData.NumInstructions++;
				}
			}
		}
	}
}

void URigVMCompiler::TraverseNoOp(const FRigVMNoOpExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	TraverseChildren(InExpr, WorkData);
}

void URigVMCompiler::TraverseVar(const FRigVMVarExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	TraverseChildren(InExpr, WorkData);

	if (WorkData.bSetupMemory)
	{
		FindOrAddRegister(InExpr, WorkData);
	}
}

void URigVMCompiler::TraverseLiteral(const FRigVMVarExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	TraverseVar(InExpr, WorkData);
}

void URigVMCompiler::TraverseAssign(const FRigVMAssignExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	TraverseChildren(InExpr, WorkData);

	ensure(InExpr->NumChildren() > 0);

	const FRigVMVarExprAST* SourceExpr = nullptr;

	const FRigVMExprAST* ChildExpr = InExpr->ChildAt(0);
	if (ChildExpr->IsA(FRigVMExprAST::EType::Var))
	{
		SourceExpr = ChildExpr->To<FRigVMVarExprAST>();
	}
	else if (ChildExpr->GetType() == FRigVMExprAST::EType::CachedValue)
	{
		SourceExpr = ChildExpr->To<FRigVMCachedValueExprAST>()->GetVarExpr();
	}
	else if (ChildExpr->GetType() == FRigVMExprAST::EType::NoOp)
	{
		ensure(ChildExpr->NumChildren() > 0);

		for (FRigVMExprAST* GrandChild : *ChildExpr)
		{
			if (GrandChild->IsA(FRigVMExprAST::EType::Var))
			{
				const FRigVMVarExprAST* VarExpr = GrandChild->To<FRigVMVarExprAST>();
				if (VarExpr->GetPin()->GetName() == TEXT("Value"))
				{
					SourceExpr = VarExpr;
					break;
				}
			}
		}

		check(SourceExpr);
	}
	else
	{
		checkNoEntry();
	}

	FRigVMOperand Source = WorkData.ExprToOperand.FindChecked(SourceExpr);

	if (WorkData.bSetupMemory)
	{
		if (Settings.ConsolidateWorkRegisters)
		{
			if (Source.GetMemoryType() != ERigVMMemoryType::Literal)
			{
				WorkData.DecRefRegister(Source.GetRegisterIndex());
			}
		}
	}
	else
	{
		const FRigVMVarExprAST* TargetExpr = InExpr->GetParent()->To<FRigVMVarExprAST>();
		FRigVMOperand Target = WorkData.ExprToOperand.FindChecked(TargetExpr);

		// if this is a copy - we should check if operands need offsets
		if (InExpr->GetType() == FRigVMExprAST::EType::Copy)
		{
			struct Local
			{
				static void SetupRegisterOffset(URigVM* VM, URigVMPin* Pin, FRigVMOperand& Operand)
				{
					URigVMPin* RootPin = Pin->GetRootPin();
					if (Pin == RootPin)
					{
						return;
					}

					FRigVMMemoryContainer& Memory = Operand.GetMemoryType() == ERigVMMemoryType::Literal ? VM->LiteralMemory : VM->WorkMemory;
					FString SegmentPath = Pin->GetSegmentPath();

					int32 ArrayIndex = INDEX_NONE;
					if (RootPin->IsArray())
					{
						FString SegmentArrayIndex = SegmentPath;
						int32 DotIndex = INDEX_NONE;
						if (SegmentPath.FindChar('.', DotIndex))
						{
							SegmentArrayIndex = SegmentPath.Left(DotIndex);
							SegmentPath = SegmentPath.Mid(DotIndex + 1);
						}
						else
						{
							SegmentPath = FString();
						}
						ArrayIndex = FCString::Atoi(*SegmentArrayIndex);
					}

					Operand = Memory.GetOperand(Operand.GetRegisterIndex(), SegmentPath, ArrayIndex);
				}
			};

			Local::SetupRegisterOffset(WorkData.VM, InExpr->GetSourcePin(), Source);
			Local::SetupRegisterOffset(WorkData.VM, InExpr->GetTargetPin(), Target);
		}

		WorkData.VM->ByteCode.AddCopyOp(Source, Target);
		WorkData.NumInstructions++;
	}
}

void URigVMCompiler::TraverseCopy(const FRigVMCopyExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	TraverseAssign(InExpr->To<FRigVMAssignExprAST>(), WorkData);
}

void URigVMCompiler::TraverseCachedValue(const FRigVMCachedValueExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	TraverseChildren(InExpr, WorkData);
}

void URigVMCompiler::TraverseExit(const FRigVMExitExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	ensure(InExpr->NumChildren() == 0);
	if (!WorkData.bSetupMemory)
	{
		WorkData.VM->ByteCode.AddExitOp();
		WorkData.NumInstructions++;
	}
}

FString URigVMCompiler::GetPinHash(URigVMPin* InPin, const FRigVMVarExprAST* InVarExpr, bool bIsDebugValue)
{
	FString Prefix = bIsDebugValue ? TEXT("DEBUG::") : TEXT("");
	FString Suffix;

	if (InPin->IsExecuteContext())
	{
		return TEXT("ExecuteContext!");
	}

	if (InVarExpr != nullptr && !bIsDebugValue)
	{
		bool bIsExecutePin = InPin->IsExecuteContext();
		bool bIsLiteral = InVarExpr->GetType() == FRigVMExprAST::EType::Literal;

		// determine if this is an initialization for an IO pin
		if (!bIsLiteral && !bIsExecutePin && (InPin->GetDirection() == ERigVMPinDirection::IO ||
			(InPin->GetDirection() == ERigVMPinDirection::Input && InPin->GetSourceLinks().Num() == 0)))
		{
			Suffix = TEXT("::IO");
		}
	}

	URigVMNode* Node = InPin->GetNode();
	if (URigVMParameterNode* ParameterNode = Cast<URigVMParameterNode>(Node))
	{
		if (InPin->GetName() == TEXT("Value"))
		{
			return FString::Printf(TEXT("%sParameter::%s%s"), *Prefix, *ParameterNode->GetParameterName().ToString(), *Suffix);
		}
	}
	else if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
	{
		if (InPin->GetName() == TEXT("Value"))
		{
			return FString::Printf(TEXT("%sVariable::%s%s"), *Prefix, *VariableNode->GetVariableName().ToString(), *Suffix);
		}
	}
	return FString::Printf(TEXT("%s%s%s"), *Prefix, *InPin->GetPinPath(), *Suffix);
}

UScriptStruct* URigVMCompiler::GetScriptStructForCPPType(const FString& InCPPType)
{
	if (InCPPType == TEXT("FRotator"))
	{
		return TBaseStructure<FRotator>::Get();
	}
	if (InCPPType == TEXT("FQuat"))
	{
		return TBaseStructure<FQuat>::Get();
	}
	if (InCPPType == TEXT("FTransform"))
	{
		return TBaseStructure<FTransform>::Get();
	}
	if (InCPPType == TEXT("FLinearColor"))
	{
		return TBaseStructure<FLinearColor>::Get();
	}
	if (InCPPType == TEXT("FColor"))
	{
		return TBaseStructure<FColor>::Get();
	}
	if (InCPPType == TEXT("FPlane"))
	{
		return TBaseStructure<FPlane>::Get();
	}
	if (InCPPType == TEXT("FVector"))
	{
		return TBaseStructure<FVector>::Get();
	}
	if (InCPPType == TEXT("FVector2D"))
	{
		return TBaseStructure<FVector2D>::Get();
	}
	if (InCPPType == TEXT("FVector4"))
	{
		return TBaseStructure<FVector4>::Get();
	}
	return nullptr;
}

TArray<URigVMPin*> URigVMCompiler::GetLinkedPins(URigVMPin* InPin, bool bInputs, bool bOutputs, bool bRecursive)
{
	TArray<URigVMPin*> LinkedPins;
	for (URigVMLink* Link : InPin->GetLinks())
	{
		if (bInputs && Link->GetTargetPin() == InPin)
		{
			LinkedPins.Add(Link->GetSourcePin());
		}
		else if (bOutputs && Link->GetSourcePin() == InPin)
		{
			LinkedPins.Add(Link->GetTargetPin());
		}
	}

	if (bRecursive)
	{
		for (URigVMPin* SubPin : InPin->GetSubPins())
		{
			LinkedPins.Append(GetLinkedPins(SubPin, bInputs, bOutputs, bRecursive));
		}
	}

	return LinkedPins;
}

uint16 URigVMCompiler::GetElementSizeFromCPPType(const FString& InCPPType, UScriptStruct* InScriptStruct)
{
	if (InScriptStruct == nullptr)
	{
		InScriptStruct = GetScriptStructForCPPType(InCPPType);
	}
	if (InScriptStruct != nullptr)
	{
		return InScriptStruct->GetStructureSize();
	}
	if (InCPPType == TEXT("bool"))
	{
		return sizeof(bool);
	}
	else if (InCPPType == TEXT("int32"))
	{
		return sizeof(int32);
	}
	if (InCPPType == TEXT("float"))
	{
		return sizeof(float);
	}
	if (InCPPType == TEXT("FName"))
	{
		return sizeof(FName);
	}
	if (InCPPType == TEXT("FString"))
	{
		return sizeof(FString);
	}

	ensure(false);
	return 0;
}

FRigVMOperand URigVMCompiler::FindOrAddRegister(const FRigVMVarExprAST* InVarExpr, FRigVMCompilerWorkData& WorkData, bool bIsDebugValue)
{
	if (!bIsDebugValue)
	{
		FRigVMOperand const* ExistingOperand = WorkData.ExprToOperand.Find(InVarExpr);
		if (ExistingOperand)
		{
			return *ExistingOperand;
		}
	}

	URigVMPin* Pin = InVarExpr->GetPin();
	FString BaseCPPType = Pin->IsArray() ? Pin->GetArrayElementCppType() : Pin->GetCPPType();
	FString Hash = GetPinHash(Pin, InVarExpr, bIsDebugValue);
	FRigVMOperand Operand;
	FString RegisterKey = Hash;

	bool bIsExecutePin = Pin->IsExecuteContext();
	bool bIsLiteral = InVarExpr->GetType() == FRigVMExprAST::EType::Literal;
	bool bIsParameter = InVarExpr->IsGraphParameter();

	FRigVMOperand const* ExistingOperand = WorkData.PinPathToOperand->Find(Hash);
	if (ExistingOperand)
	{
		if (!bIsDebugValue)
		{
			WorkData.ExprToOperand.Add(InVarExpr, *ExistingOperand);
		}
		return *ExistingOperand;
	}

	// create remaining operands / registers
	if (!Operand.IsValid())
	{
		FName RegisterName = *RegisterKey;

		TArray<FString> DefaultValues;
		if (Pin->IsArray())
		{
			if (Pin->GetDirection() == ERigVMPinDirection::Hidden)
			{
				ensure(WorkData.DefaultStructs.Num() > 0);
				FRigVMStruct* VMStruct = WorkData.DefaultStructs.Last();

				int32 DesiredArraySize = VMStruct->GetMaxArraySize(Pin->GetFName(), WorkData.RigVMUserData);
				ensure(DesiredArraySize > 0);

				DefaultValues = URigVMController::SplitDefaultValue(Pin->GetDefaultValue());

				if (DefaultValues.Num() != DesiredArraySize)
				{
					FString DefaultValue;
					if (Pin->GetArraySize() > 0)
					{
						DefaultValue = Pin->GetSubPins()[0]->GetDefaultValue();
					}

					DefaultValues.Reset();
					for (int32 Index = 0; Index < DesiredArraySize; Index++)
					{
						DefaultValues.Add(DefaultValue);
					}
				}
			}
			else
			{
				DefaultValues = URigVMController::SplitDefaultValue(Pin->GetDefaultValue());
			}

			while (DefaultValues.Num() < Pin->GetSubPins().Num())
			{
				DefaultValues.Add(FString());
			}
		}
		else
		{
			DefaultValues.Add(Pin->GetDefaultValue());
		}

		UScriptStruct* ScriptStruct = Pin->GetScriptStruct();
		if (ScriptStruct == nullptr)
		{
			ScriptStruct = GetScriptStructForCPPType(BaseCPPType);
		}

		FRigVMMemoryContainer& Memory = (bIsLiteral && Settings.SplitLiteralsFromWorkMemory) ? WorkData.VM->LiteralMemory : WorkData.VM->WorkMemory;

		// look for a register to reuse
		if (!bIsLiteral && !bIsDebugValue && Settings.ConsolidateWorkRegisters &&
			(Pin->GetDirection() == ERigVMPinDirection::Output || Pin->GetDirection() == ERigVMPinDirection::IO) &&
			!bIsExecutePin)
		{
			for (TPair<int32, int32> Pair : WorkData.RegisterRefCount)
			{
				int32 ExistingRegisterIndex = Pair.Key;
				if (Pair.Value > 0)
				{
					continue;
				}

				if (Pin->IsArray())
				{
					if (!Memory[ExistingRegisterIndex].IsArray())
					{
						continue;
					}

					if (Pin->GetSubPins().Num() != Memory[ExistingRegisterIndex].ElementCount)
					{
						continue;
					}
				}

				if (ScriptStruct)
				{
					if (Memory[ExistingRegisterIndex].ScriptStructIndex == INDEX_NONE)
					{
						continue;
					}
					if (Memory.GetScriptStruct(ExistingRegisterIndex) != ScriptStruct)
					{
						continue;
					}
				}
				else if (Pin->GetCPPType() == TEXT("FString"))
				{
					if (Memory[ExistingRegisterIndex].Type != ERigVMRegisterType::String)
					{
						continue;
					}
				}
				else if (Pin->GetCPPType() == TEXT("FName"))
				{
					if (Memory[ExistingRegisterIndex].Type != ERigVMRegisterType::Name)
					{
						continue;
					}
				}
				else
				{
					if (Memory[ExistingRegisterIndex].Type != ERigVMRegisterType::Plain)
					{
						continue;
					}

					if (Pin->GetCPPType() == TEXT("bool"))
					{
						if (Memory[ExistingRegisterIndex].ElementSize != sizeof(bool))
						{
							continue;
						}
					}
					else if (Pin->GetCPPType() == TEXT("int32"))
					{
						if (Memory[ExistingRegisterIndex].ElementSize != sizeof(int32))
						{
							continue;
						}
					}
					else if (Pin->GetCPPType() == TEXT("float"))
					{
						if (Memory[ExistingRegisterIndex].ElementSize != sizeof(float))
						{
							continue;
						}
					}
					else if (UEnum* Enum = Pin->GetEnum())
					{
						if (Memory[ExistingRegisterIndex].ElementSize != sizeof(uint8))
						{
							continue;
						}
					}
					else
					{
						ensure(false);
					}
				}

				WorkData.IncRefRegister(ExistingRegisterIndex, 1 + Pin->GetLinkedTargetPins(true).Num());
				Operand = Memory.GetOperand(ExistingRegisterIndex);
				break;
			}
		}

		if (!Operand.IsValid())
		{
			if (ScriptStruct)
			{
				TArray<uint8, TAlignedHeapAllocator<16>> Data;
				Data.AddUninitialized(ScriptStruct->GetStructureSize() * DefaultValues.Num());
				uint8* Ptr = Data.GetData();

				for (FString DefaultValue : DefaultValues)
				{
					ScriptStruct->InitializeStruct(Ptr, 1);
					if (!DefaultValue.IsEmpty() && DefaultValue != TEXT("()"))
					{
						FRigVMCompilerImportErrorContext ErrorPipe(this);
						ScriptStruct->ImportText(*DefaultValue, Ptr, nullptr, PPF_None, &ErrorPipe, ScriptStruct->GetName());
					}
					Ptr = Ptr + ScriptStruct->GetStructureSize();
				}

				int32 Register = Memory.AddStructArray(RegisterName, ScriptStruct, DefaultValues.Num(), Data.GetData(), 1);
				ScriptStruct->DestroyStruct(Data.GetData(), DefaultValues.Num());

				Operand = Memory.GetOperand(Register);
			}
			else if (UEnum* Enum = Pin->GetEnum())
			{
				TArray<uint8> Values;
				for (FString DefaultValue : DefaultValues)
				{
					if (DefaultValue.IsEmpty())
					{
						Values.Add((uint8)Enum->GetValueByIndex(0));
					}
					else
					{
						Values.Add((uint8)Enum->GetValueByNameString(DefaultValue));
					}
				}
				int32 Register = Memory.AddPlainArray<uint8>(RegisterName, Values, 1);
				Operand = Memory.GetOperand(Register);
			}
			else if (BaseCPPType == TEXT("bool"))
			{
				TArray<bool> Values;
				for (FString DefaultValue : DefaultValues)
				{
					Values.Add((DefaultValue == TEXT("True")) || (DefaultValue == TEXT("true")) || (DefaultValue == TEXT("1")));
				}
				int32 Register = Memory.AddPlainArray<bool>(RegisterName, Values, 1);
				Operand = Memory.GetOperand(Register);
			}
			else if (BaseCPPType == TEXT("int32"))
			{
				TArray<int32> Values;
				for (FString DefaultValue : DefaultValues)
				{
					if (DefaultValue.IsEmpty())
					{
						Values.Add(0);
					}
					else
					{
						Values.Add(FCString::Atoi(*DefaultValue));
					}
				}
				int32 Register = Memory.AddPlainArray<int32>(RegisterName, Values, 1);
				Operand = Memory.GetOperand(Register);
			}
			else if (BaseCPPType == TEXT("float"))
			{
				TArray<float> Values;
				for (FString DefaultValue : DefaultValues)
				{
					if (DefaultValue.IsEmpty())
					{
						Values.Add(0.f);
					}
					else
					{
						Values.Add(FCString::Atof(*DefaultValue));
					}
				}
				int32 Register = Memory.AddPlainArray<float>(RegisterName, Values, 1);
				Operand = Memory.GetOperand(Register);
			}
			else if (BaseCPPType == TEXT("FName"))
			{
				TArray<FName> Values;
				for (FString DefaultValue : DefaultValues)
				{
					Values.Add(*DefaultValue);
				}
				int32 Register = Memory.AddPlainArray<FName>(RegisterName, Values, 1);
				Operand = Memory.GetOperand(Register);
			}
			else if (BaseCPPType == TEXT("FString"))
			{
				int32 Register = Memory.AddPlainArray<FString>(RegisterName, DefaultValues, 1);
				Operand = Memory.GetOperand(Register);
			}
			else
			{
				ensure(false);
			}

			Memory[Operand.GetRegisterIndex()].bIsArray = Pin->IsArray();

			if (!bIsParameter && !bIsLiteral && !bIsDebugValue && Settings.ConsolidateWorkRegisters && !bIsExecutePin && Pin->GetDirection() != ERigVMPinDirection::Hidden)
			{
				WorkData.IncRefRegister(Operand.GetRegisterIndex(), 1 + Pin->GetLinkedTargetPins(true).Num());
			}
		}

		if (bIsParameter)
		{
			URigVMParameterNode* ParameterNode = Cast<URigVMParameterNode>(Pin->GetNode());
			check(ParameterNode);
			FName Name = ParameterNode->GetParameterName();
			ERigVMParameterType ParameterType = ParameterNode->IsInput() ? ERigVMParameterType::Input : ERigVMParameterType::Output;
			FRigVMParameter Parameter(ParameterType, Name, Operand.GetRegisterIndex(), Pin->GetCPPType(), ScriptStruct);
			WorkData.VM->ParametersNameMap.FindOrAdd(Parameter.Name) = WorkData.VM->Parameters.Add(Parameter);
		}
	}
	ensure(Operand.IsValid());

	WorkData.PinPathToOperand->Add(Hash, Operand);
	WorkData.ExprToOperand.Add(InVarExpr, Operand);

	return Operand;
}

void URigVMCompiler::ReportInfo(const FString& InMessage)
{
	if (!Settings.SurpressInfoMessages)
	{
		UE_LOG(LogRigVMDeveloper, Display, TEXT("%s"), *InMessage);
	}
}

void URigVMCompiler::ReportWarning(const FString& InMessage)
{
	FScriptExceptionHandler::Get().HandleException(ELogVerbosity::Warning, *InMessage, *FString());
}

void URigVMCompiler::ReportError(const FString& InMessage)
{
	FScriptExceptionHandler::Get().HandleException(ELogVerbosity::Error, *InMessage, *FString());
}
