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
	, ConsolidateWorkRegisters(false)
	, ASTSettings(FRigVMParserASTSettings::Optimized())
	, SetupNodeInstructionIndex(true)
{
}

int32 FRigVMCompilerWorkData::IncRefRegister(int32 InRegister, int32 InIncrement)
{
	ensure(InIncrement > 0);
	if (int32* RefCountPtr = RegisterRefCount.Find(InRegister))
	{
		*RefCountPtr += InIncrement;
		RecordRefCountStep(InRegister, *RefCountPtr);
		return *RefCountPtr;
	}
	RegisterRefCount.Add(InRegister, InIncrement);
	RecordRefCountStep(InRegister, InIncrement);
	return InIncrement;
}

int32 FRigVMCompilerWorkData::DecRefRegister(int32 InRegister, int32 InDecrement)
{
	ensure(InDecrement > 0);
	if (int32* RefCountPtr = RegisterRefCount.Find(InRegister))
	{
		int32& RefCount = *RefCountPtr;
		RefCount = FMath::Max<int32>(0, RefCount - InDecrement);
		RecordRefCountStep(InRegister, RefCount);
		return RefCount;
	}
	return 0;
}

void FRigVMCompilerWorkData::RecordRefCountStep(int32  InRegister, int32 InRefCount)
{
	const FName& Name = VM->GetWorkMemory().Registers[InRegister].Name;
	RefCountSteps.Add(TPair<FName, int32>(Name, InRefCount));
}

URigVMCompiler::URigVMCompiler()
{
}

bool URigVMCompiler::Compile(URigVMGraph* InGraph, URigVMController* InController, URigVM* OutVM, const TArray<FRigVMExternalVariable>& InExternalVariables, const TArray<FRigVMUserDataArray>& InRigVMUserData, TMap<FString, FRigVMOperand>* OutOperands, TSharedPtr<FRigVMParserAST> InAST)
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

	TArray<FRigVMUserDataArray> UserData = InRigVMUserData;
	if (UserData.Num() == 0)
	{
		UserData.Add(FRigVMUserDataArray());
	}

	OutVM->Reset();

	if (Settings.SetupNodeInstructionIndex)
	{
		for (URigVMNode* Node : InGraph->Nodes)
		{
			Node->InstructionIndex = INDEX_NONE;
		}
	}

	TMap<FString, FRigVMOperand> LocalOperands;
	if (OutOperands == nullptr)
	{
		OutOperands = &LocalOperands;
	}
	OutOperands->Reset();

	OutVM->ClearExternalVariables();
	for (const FRigVMExternalVariable& ExternalVariable : InExternalVariables)
	{
		FRigVMOperand Operand = OutVM->AddExternalVariable(ExternalVariable);
		FString Hash = FString::Printf(TEXT("Variable::%s"), *ExternalVariable.Name.ToString());
		OutOperands->Add(Hash, Operand);
	}

	TSharedPtr<FRigVMParserAST> AST = InAST;
	if (!AST.IsValid())
	{
		AST = MakeShareable(new FRigVMParserAST(InGraph, InController, Settings.ASTSettings, InExternalVariables, UserData));
		InGraph->RuntimeAST = AST;
		//UE_LOG(LogRigVMDeveloper, Display, TEXT("%s"), *AST->DumpDot());
	}
	ensure(AST.IsValid());

	FRigVMCompilerWorkData WorkData;
	WorkData.VM = OutVM;
	WorkData.PinPathToOperand = OutOperands;
	WorkData.RigVMUserData = UserData[0];
	WorkData.bSetupMemory = true;

	UE_LOG_RIGVMMEMORY(TEXT("RigVMCompiler: Begin '%s'..."), *InGraph->GetPathName());

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

	if (WorkData.VM->GetByteCode().GetInstructions().Num() == 0)
	{
		WorkData.VM->GetByteCode().AddExitOp();
	}

	WorkData.VM->GetByteCode().AlignByteCode();

	// loop over all nodes once more and setup the instruction index for reroute nodes
	if (Settings.SetupNodeInstructionIndex)
	{
		struct Local
		{
			static int32 GetInstructionIndex(URigVMNode* InNode, TMap<URigVMNode*, int32>& InOutKnownInstructionIndex)
			{
				if (InNode->GetInstructionIndex() != INDEX_NONE)
				{
					return InNode->GetInstructionIndex();
				}

				if (int32* InstructionIndexPtr = InOutKnownInstructionIndex.Find(InNode))
				{
					return *InstructionIndexPtr;
				}

				InOutKnownInstructionIndex.Add(InNode, INDEX_NONE);

				if (URigVMRerouteNode* RerouteNode = Cast<URigVMRerouteNode>(InNode))
				{
					TArray<URigVMNode*> TargetNodes = RerouteNode->GetLinkedTargetNodes();
					if (TargetNodes.Num() > 0)
					{
						TArray<URigVMNode*> SourceNodes = RerouteNode->GetLinkedSourceNodes();
						if (SourceNodes.Num() == 0)
						{
							for (URigVMNode* TargetNode : TargetNodes)
							{
								int32 InstructionIndex = GetInstructionIndex(TargetNode, InOutKnownInstructionIndex);
								InOutKnownInstructionIndex.FindOrAdd(TargetNode) = InstructionIndex;

								if (InstructionIndex != INDEX_NONE)
								{
									InOutKnownInstructionIndex.FindOrAdd(InNode) = InstructionIndex;
									return InstructionIndex;
								}
							}
						}
						else
						{
							for (URigVMNode* SourceNode : SourceNodes)
							{
								int32 InstructionIndex = GetInstructionIndex(SourceNode, InOutKnownInstructionIndex);
								InOutKnownInstructionIndex.FindOrAdd(SourceNode) = InstructionIndex;

								if (InstructionIndex != INDEX_NONE)
								{
									InOutKnownInstructionIndex.FindOrAdd(InNode) = InstructionIndex;
									return InstructionIndex;
								}
							}
						}
					}
				}

				return INDEX_NONE;
			}
		};

		TMap<URigVMNode*, int32> KnownInstructionIndex;
		for (URigVMNode* Node : InGraph->Nodes)
		{
			Node->InstructionIndex = Local::GetInstructionIndex(Node, KnownInstructionIndex);
		}
	}


	UE_LOG_RIGVMMEMORY(TEXT("RigVMCompiler: Finished '%s'."), *InGraph->GetPathName());

	return true;
}

void URigVMCompiler::TraverseExpression(const FRigVMExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	if (WorkData.ExprToSkip.Contains(InExpr))
	{
		return;
	}

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
			const FRigVMCallExternExprAST* CallExternExpr = InExpr->To<FRigVMCallExternExprAST>();
			if (URigVMStructNode* StructNode = Cast<URigVMStructNode>(CallExternExpr->GetNode()))
			{
				if (StructNode->IsLoopNode())
				{
					TraverseForLoop(CallExternExpr, WorkData);
					break;
				}
			}

			TraverseCallExtern(CallExternExpr, WorkData);
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
		case FRigVMExprAST::EType::Branch:
		{
			TraverseBranch(InExpr->To<FRigVMBranchExprAST>(), WorkData);
			break;
		}
		case FRigVMExprAST::EType::If:
		{
			TraverseIf(InExpr->To<FRigVMIfExprAST>(), WorkData);
			break;
		}
		case FRigVMExprAST::EType::Select:
		{
			TraverseSelect(InExpr->To<FRigVMSelectExprAST>(), WorkData);
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
	if (InExpr->IsObsolete())
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
		TSharedPtr<FStructOnScope> DefaultStruct = StructNode->ConstructStructInstance();
		WorkData.DefaultStructs.Add(DefaultStruct);
		TraverseChildren(InExpr, WorkData);
		WorkData.DefaultStructs.Pop();
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
		WorkData.VM->GetByteCode().AddExecuteOp(FunctionIndex, Operands);
		
		int32 EntryInstructionIndex = WorkData.VM->GetByteCode().GetNumInstructions() - 1;
		FName Entryname = StructNode->GetEventName();

		if (WorkData.VM->GetByteCode().FindEntryIndex(Entryname) == INDEX_NONE)
		{
			FRigVMByteCodeEntry Entry;
			Entry.Name = Entryname;
			Entry.InstructionIndex = EntryInstructionIndex;
			WorkData.VM->GetByteCode().Entries.Add(Entry);
		}

		if (Settings.SetupNodeInstructionIndex)
		{
			StructNode->InstructionIndex = EntryInstructionIndex;
		}

		TraverseChildren(InExpr, WorkData);
	}
}

int32 URigVMCompiler::TraverseCallExtern(const FRigVMCallExternExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	URigVMStructNode* StructNode = Cast<URigVMStructNode>(InExpr->GetNode());

	int32 InstructionIndex = INDEX_NONE;

	if (WorkData.bSetupMemory)
	{
		TSharedPtr<FStructOnScope> DefaultStruct = StructNode->ConstructStructInstance();
		WorkData.DefaultStructs.Add(DefaultStruct);
		TraverseChildren(InExpr, WorkData);
		WorkData.DefaultStructs.Pop();

		if (Settings.EnablePinWatches)
		{
			for (URigVMPin* Pin : StructNode->Pins)
			{
				if (Pin->RequiresWatch())
				{
					FRigVMVarExprAST TempVarExpr(FRigVMExprAST::EType::Var, Pin);
					TempVarExpr.ParserPtr = InExpr->GetParser();
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
		WorkData.VM->GetByteCode().AddExecuteOp(FunctionIndex, Operands);
		InstructionIndex = WorkData.VM->GetByteCode().GetNumInstructions() - 1;
		if (Settings.SetupNodeInstructionIndex)
		{
			StructNode->InstructionIndex = InstructionIndex;
		}

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
					FRigVMOperand SourceRootOperand(Source.GetMemoryType(), Source.GetRegisterIndex(), INDEX_NONE);
					WorkData.VM->GetByteCode().AddCopyOp(SourceRootOperand, *Target);
				}
			}
		}
	}

	return InstructionIndex;
}

void URigVMCompiler::TraverseForLoop(const FRigVMCallExternExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	if (WorkData.bSetupMemory)
	{
		TraverseCallExtern(InExpr, WorkData);
		return;
	}

	URigVMStructNode* StructNode = Cast<URigVMStructNode>(InExpr->GetNode());

	const FRigVMVarExprAST* CompletedExpr = InExpr->FindVarWithPinName(FRigVMStruct::ForLoopCompletedPinName);
	check(CompletedExpr);
	const FRigVMVarExprAST* ExecuteExpr = InExpr->FindVarWithPinName(FRigVMStruct::ExecuteContextName);
	check(ExecuteExpr);
	WorkData.ExprToSkip.AddUnique(CompletedExpr);
	WorkData.ExprToSkip.AddUnique(ExecuteExpr);

	// set the index to 0
	const FRigVMVarExprAST* IndexExpr = InExpr->FindVarWithPinName(FRigVMStruct::ForLoopIndexPinName);
	check(IndexExpr);
	FRigVMOperand IndexOperand = WorkData.ExprToOperand.FindChecked(IndexExpr);
	WorkData.VM->GetByteCode().AddZeroOp(IndexOperand);

	// call the for loop compute
	int32 ForLoopInstructionIndex = TraverseCallExtern(InExpr, WorkData);

	// set up the jump forward (jump out of the loop)
	const FRigVMVarExprAST* ContinueLoopExpr = InExpr->FindVarWithPinName(FRigVMStruct::ForLoopContinuePinName);
	check(ContinueLoopExpr);
	FRigVMOperand ContinueLoopOperand = WorkData.ExprToOperand.FindChecked(ContinueLoopExpr);

	uint64 JumpToEndByte = WorkData.VM->GetByteCode().AddJumpIfOp(ERigVMOpCode::JumpForwardIf, 0, ContinueLoopOperand, false);
	int32 JumpToEndInstruction = WorkData.VM->GetByteCode().GetNumInstructions() - 1;

	// begin the loop's block
	const FRigVMVarExprAST* CountExpr = InExpr->FindVarWithPinName(FRigVMStruct::ForLoopCountPinName);
	check(CountExpr);
	FRigVMOperand CountOperand = WorkData.ExprToOperand.FindChecked(CountExpr);
	WorkData.VM->GetByteCode().AddBeginBlockOp(CountOperand, IndexOperand);

	// traverse the body of the loop
	WorkData.ExprToSkip.Remove(ExecuteExpr);
	TraverseExpression(ExecuteExpr, WorkData);

	// end the loop's block
	WorkData.VM->GetByteCode().AddEndBlockOp();

	// increment the index
	WorkData.VM->GetByteCode().AddIncrementOp(IndexOperand);

	// jump to the beginning of the loop
	int32 JumpToStartInstruction = WorkData.VM->GetByteCode().GetNumInstructions();
	WorkData.VM->GetByteCode().AddJumpOp(ERigVMOpCode::JumpBackward, JumpToStartInstruction - ForLoopInstructionIndex);

	// update the jump operator with the right address
	int32 InstructionsToEnd = WorkData.VM->GetByteCode().GetNumInstructions() - JumpToEndInstruction;
	WorkData.VM->GetByteCode().GetOpAt<FRigVMJumpIfOp>(JumpToEndByte).InstructionIndex = InstructionsToEnd;

	// now traverse everything else connected to the completed pin
	WorkData.ExprToSkip.Remove(CompletedExpr);
	TraverseExpression(CompletedExpr, WorkData);
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
				if (VarExpr->GetPin()->GetName() == TEXT("Value") ||
					VarExpr->GetPin()->GetName() == TEXT("EnumIndex"))
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
		const FRigVMVarExprAST* TargetExpr = InExpr->GetFirstParentOfType(FRigVMVarExprAST::EType::Var)->To<FRigVMVarExprAST>();
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

					if (URigVMSelectNode* SelectNode = Cast<URigVMSelectNode>(RootPin->GetNode()))
					{
						if (Pin->GetParentPin() == RootPin && RootPin->GetName() == URigVMSelectNode::ValueName)
						{
							return;
						}
					}

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

					if (Operand.GetMemoryType() == ERigVMMemoryType::External)
					{
						const FRigVMExternalVariable& ExternalVariable = VM->GetExternalVariables()[Operand.GetRegisterIndex()];
						UScriptStruct* ScriptStruct = CastChecked<UScriptStruct>(ExternalVariable.TypeObject);
						int32 RegisterOffset = VM->GetWorkMemory().GetOrAddRegisterOffset(INDEX_NONE, ScriptStruct, SegmentPath, ArrayIndex == INDEX_NONE ? 0 : ArrayIndex, ExternalVariable.Size);
						Operand = FRigVMOperand(ERigVMMemoryType::External, Operand.GetRegisterIndex(), RegisterOffset);
					}
					else
					{
						FRigVMMemoryContainer& Memory = Operand.GetMemoryType() == ERigVMMemoryType::Literal ? VM->GetLiteralMemory() : VM->GetWorkMemory();
						Operand = Memory.GetOperand(Operand.GetRegisterIndex(), SegmentPath, ArrayIndex);
					}
				}
			};

			Local::SetupRegisterOffset(WorkData.VM, InExpr->GetSourcePin(), Source);
			Local::SetupRegisterOffset(WorkData.VM, InExpr->GetTargetPin(), Target);
		}

		WorkData.VM->GetByteCode().AddCopyOp(Source, Target);
		int32 InstructionIndex = WorkData.VM->GetByteCode().GetNumInstructions() - 1;

		if (Settings.SetupNodeInstructionIndex)
		{
			if (Source.GetMemoryType() == ERigVMMemoryType::External)
			{
				if (URigVMPin* SourcePin = InExpr->GetSourcePin())
				{
					if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(SourcePin->GetNode()))
					{
						VariableNode->InstructionIndex = InstructionIndex;
					}
				}
			}
			if (Target.GetMemoryType() == ERigVMMemoryType::External)
			{
				if (URigVMPin* TargetPin = InExpr->GetTargetPin())
				{
					if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(TargetPin->GetNode()))
					{
						VariableNode->InstructionIndex = InstructionIndex;
					}
				}
			}
		}

		if (Cast<URigVMVariableNode>(SourceExpr->GetPin()->GetNode()))
		{
			if (SourceExpr->GetPin()->RequiresWatch() && Settings.EnablePinWatches)
			{
				FRigVMOperand WatchOperand = FindOrAddRegister(SourceExpr, WorkData, true /*debug watch*/);
				if (WatchOperand.IsValid())
				{
					FRigVMOperand SourceRootOperand(Source.GetMemoryType(), Source.GetRegisterIndex(), INDEX_NONE);
					WorkData.VM->GetByteCode().AddCopyOp(SourceRootOperand, WatchOperand);
				}
			}
		}

		if (Cast<URigVMVariableNode>(TargetExpr->GetPin()->GetNode()))
		{
			if (TargetExpr->GetPin()->RequiresWatch() && Settings.EnablePinWatches)
			{
				FRigVMOperand WatchOperand = FindOrAddRegister(TargetExpr, WorkData, true /*debug watch*/);
				if (WatchOperand.IsValid())
				{
					FRigVMOperand TargetRootOperand(Target.GetMemoryType(), Target.GetRegisterIndex(), INDEX_NONE);
					WorkData.VM->GetByteCode().AddCopyOp(TargetRootOperand, WatchOperand);
				}
			}
		}
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
		WorkData.VM->GetByteCode().AddExitOp();
	}
}

void URigVMCompiler::TraverseBranch(const FRigVMBranchExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	ensure(InExpr->NumChildren() == 4);

	if (WorkData.bSetupMemory)
	{
		TraverseChildren(InExpr, WorkData);
		return;
	}

	URigVMBranchNode* BranchNode = Cast<URigVMBranchNode>(InExpr->GetNode());

	const FRigVMVarExprAST* ExecuteContextExpr = InExpr->ChildAt<FRigVMVarExprAST>(0);
	const FRigVMVarExprAST* ConditionExpr = InExpr->ChildAt<FRigVMVarExprAST>(1);
	const FRigVMVarExprAST* TrueExpr = InExpr->ChildAt<FRigVMVarExprAST>(2);
	const FRigVMVarExprAST* FalseExpr = InExpr->ChildAt<FRigVMVarExprAST>(3);

	// traverse the condition first
	TraverseExpression(ConditionExpr, WorkData);

	if (ConditionExpr->IsA(FRigVMExprAST::CachedValue))
	{
		ConditionExpr = ConditionExpr->To<FRigVMCachedValueExprAST>()->GetVarExpr();
	}

	FRigVMOperand& ConditionOperand = WorkData.ExprToOperand.FindChecked(ConditionExpr);

	// setup the first jump
	uint64 JumpToFalseByte = WorkData.VM->GetByteCode().AddJumpIfOp(ERigVMOpCode::JumpForwardIf, 1, ConditionOperand, false);
	int32 JumpToFalseInstruction = WorkData.VM->GetByteCode().GetNumInstructions() - 1;
	if (Settings.SetupNodeInstructionIndex)
	{
		BranchNode->InstructionIndex = JumpToFalseInstruction;
	}

	// traverse the true case
	TraverseExpression(TrueExpr, WorkData);

	uint64 JumpToEndByte = WorkData.VM->GetByteCode().AddJumpOp(ERigVMOpCode::JumpForward, 1);
	int32 JumpToEndInstruction = WorkData.VM->GetByteCode().GetNumInstructions() - 1;

	// correct the jump to false instruction index
	int32 NumInstructionsInTrueCase = WorkData.VM->GetByteCode().GetNumInstructions() - JumpToFalseInstruction;
	WorkData.VM->GetByteCode().GetOpAt<FRigVMJumpIfOp>(JumpToFalseByte).InstructionIndex = NumInstructionsInTrueCase;

	// traverse the false case
	TraverseExpression(FalseExpr, WorkData);

	// correct the jump to end instruction index
	int32 NumInstructionsInFalseCase = WorkData.VM->GetByteCode().GetNumInstructions() - JumpToEndInstruction;
	WorkData.VM->GetByteCode().GetOpAt<FRigVMJumpOp>(JumpToEndByte).InstructionIndex = NumInstructionsInFalseCase;
}

void URigVMCompiler::TraverseIf(const FRigVMIfExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	ensure(InExpr->NumChildren() == 4);

	if (WorkData.bSetupMemory)
	{
		TraverseChildren(InExpr, WorkData);
		return;
	}

	URigVMIfNode* IfNode = Cast<URigVMIfNode>(InExpr->GetNode());

	const FRigVMVarExprAST* ConditionExpr = InExpr->ChildAt<FRigVMVarExprAST>(0);
	const FRigVMVarExprAST* TrueExpr = InExpr->ChildAt<FRigVMVarExprAST>(1);
	const FRigVMVarExprAST* FalseExpr = InExpr->ChildAt<FRigVMVarExprAST>(2);
	const FRigVMVarExprAST* ResultExpr = InExpr->ChildAt<FRigVMVarExprAST>(3);

	// traverse the condition first
	TraverseExpression(ConditionExpr, WorkData);

	if (ConditionExpr->IsA(FRigVMExprAST::CachedValue))
	{
		ConditionExpr = ConditionExpr->To<FRigVMCachedValueExprAST>()->GetVarExpr();
	}

	FRigVMOperand& ConditionOperand = WorkData.ExprToOperand.FindChecked(ConditionExpr);
	FRigVMOperand& ResultOperand = WorkData.ExprToOperand.FindChecked(ResultExpr);

	// setup the first jump
	uint64 JumpToFalseByte = WorkData.VM->GetByteCode().AddJumpIfOp(ERigVMOpCode::JumpForwardIf, 1, ConditionOperand, false);
	int32 JumpToFalseInstruction = WorkData.VM->GetByteCode().GetNumInstructions() - 1;
	if (Settings.SetupNodeInstructionIndex)
	{
		IfNode->InstructionIndex = JumpToFalseInstruction;
	}

	// traverse the true case
	TraverseExpression(TrueExpr, WorkData);

	if (TrueExpr->IsA(FRigVMExprAST::CachedValue))
	{
		TrueExpr = TrueExpr->To<FRigVMCachedValueExprAST>()->GetVarExpr();
	}

	FRigVMOperand& TrueOperand = WorkData.ExprToOperand.FindChecked(TrueExpr);

	WorkData.VM->GetByteCode().AddCopyOp(TrueOperand, ResultOperand);

	uint64 JumpToEndByte = WorkData.VM->GetByteCode().AddJumpOp(ERigVMOpCode::JumpForward, 1);
	int32 JumpToEndInstruction = WorkData.VM->GetByteCode().GetNumInstructions() - 1;

	// correct the jump to false instruction index
	int32 NumInstructionsInTrueCase = WorkData.VM->GetByteCode().GetNumInstructions() - JumpToFalseInstruction;
	WorkData.VM->GetByteCode().GetOpAt<FRigVMJumpIfOp>(JumpToFalseByte).InstructionIndex = NumInstructionsInTrueCase;

	// traverse the false case
	TraverseExpression(FalseExpr, WorkData);

	if (FalseExpr->IsA(FRigVMExprAST::CachedValue))
	{
		FalseExpr = FalseExpr->To<FRigVMCachedValueExprAST>()->GetVarExpr();
	}

	FRigVMOperand& FalseOperand = WorkData.ExprToOperand.FindChecked(FalseExpr);

	WorkData.VM->GetByteCode().AddCopyOp(FalseOperand, ResultOperand);

	// correct the jump to end instruction index
	int32 NumInstructionsInFalseCase = WorkData.VM->GetByteCode().GetNumInstructions() - JumpToEndInstruction;
	WorkData.VM->GetByteCode().GetOpAt<FRigVMJumpOp>(JumpToEndByte).InstructionIndex = NumInstructionsInFalseCase;
}

void URigVMCompiler::TraverseSelect(const FRigVMSelectExprAST* InExpr, FRigVMCompilerWorkData& WorkData)
{
	URigVMSelectNode* SelectNode = Cast<URigVMSelectNode>(InExpr->GetNode());
	int32 NumCases = SelectNode->FindPin(URigVMSelectNode::ValueName)->GetArraySize();

	if (WorkData.bSetupMemory)
	{
		TraverseChildren(InExpr, WorkData);

		// setup literals for each index (we don't need zero)
		for (int32 CaseIndex = 1; CaseIndex < NumCases; CaseIndex++)
		{
			if (!WorkData.IntegerLiterals.Contains(CaseIndex))
			{
				FName LiteralName = *FString::FromInt(CaseIndex);
				int32 Register = WorkData.VM->GetLiteralMemory().Add<int32>(LiteralName, CaseIndex);
				FRigVMOperand Operand = WorkData.VM->GetLiteralMemory().GetOperand(Register);
				WorkData.IntegerLiterals.Add(CaseIndex, Operand);
			}
		}

		if (!WorkData.ComparisonLiteral.IsValid())
		{
			int32 Register = WorkData.VM->GetLiteralMemory().Add<bool>(FName(TEXT("IntEquals")), false);
			WorkData.ComparisonLiteral = WorkData.VM->GetLiteralMemory().GetOperand(Register);
		}
		return;
	}

	const FRigVMVarExprAST* IndexExpr = InExpr->ChildAt<FRigVMVarExprAST>(0);
	TArray<const FRigVMVarExprAST*> CaseExpressions;
	for (int32 CaseIndex = 0; CaseIndex < NumCases; CaseIndex++)
	{
		CaseExpressions.Add(InExpr->ChildAt<FRigVMVarExprAST>(CaseIndex + 1));
	}

	const FRigVMVarExprAST* ResultExpr = InExpr->ChildAt<FRigVMVarExprAST>(InExpr->NumChildren() - 1);

	// traverse the condition first
	TraverseExpression(IndexExpr, WorkData);

	// this can happen if the optimizer doesn't remove it
	if (CaseExpressions.Num() == 0)
	{
		return;
	}

	if (IndexExpr->IsA(FRigVMExprAST::CachedValue))
	{
		IndexExpr = IndexExpr->To<FRigVMCachedValueExprAST>()->GetVarExpr();
	}

	FRigVMOperand& IndexOperand = WorkData.ExprToOperand.FindChecked(IndexExpr);
	FRigVMOperand& ResultOperand = WorkData.ExprToOperand.FindChecked(ResultExpr);

	// setup the jumps for each case
	TArray<uint64> JumpToCaseBytes;
	TArray<int32> JumpToCaseInstructions;
	JumpToCaseBytes.Add(0);
	JumpToCaseInstructions.Add(0);

	for (int32 CaseIndex = 1; CaseIndex < NumCases; CaseIndex++)
	{
		// compare and jump eventually
		WorkData.VM->GetByteCode().AddEqualsOp(IndexOperand, WorkData.IntegerLiterals.FindChecked(CaseIndex), WorkData.ComparisonLiteral);
		if (CaseIndex == 1 && Settings.SetupNodeInstructionIndex)
		{
			SelectNode->InstructionIndex = WorkData.VM->GetByteCode().GetNumInstructions() - 1;
		}

		uint64 JumpByte = WorkData.VM->GetByteCode().AddJumpIfOp(ERigVMOpCode::JumpForwardIf, 1, WorkData.ComparisonLiteral, true);
		int32 JumpInstruction = WorkData.VM->GetByteCode().GetNumInstructions() - 1;
		JumpToCaseBytes.Add(JumpByte);
		JumpToCaseInstructions.Add(JumpInstruction);
	}

	TArray<uint64> JumpToEndBytes;
	TArray<int32> JumpToEndInstructions;

	for (int32 CaseIndex = 0; CaseIndex < NumCases; CaseIndex++)
	{
		if (CaseIndex > 0)
		{
			int32 NumInstructionsInCase = WorkData.VM->GetByteCode().GetNumInstructions() - JumpToCaseInstructions[CaseIndex];
			WorkData.VM->GetByteCode().GetOpAt<FRigVMJumpIfOp>(JumpToCaseBytes[CaseIndex]).InstructionIndex = NumInstructionsInCase;
		}

		TraverseExpression(CaseExpressions[CaseIndex], WorkData);

		// add copy op to copy the result
		FRigVMOperand& CaseOperand = WorkData.ExprToOperand.FindChecked(CaseExpressions[CaseIndex]);
		WorkData.VM->GetByteCode().AddCopyOp(CaseOperand, ResultOperand);
		
		if (CaseIndex < NumCases - 1)
		{
			uint64 JumpByte = WorkData.VM->GetByteCode().AddJumpOp(ERigVMOpCode::JumpForward, 1);
			int32 JumpInstruction = WorkData.VM->GetByteCode().GetNumInstructions() - 1;
			JumpToEndBytes.Add(JumpByte);
			JumpToEndInstructions.Add(JumpInstruction);
		}
	}

	for (int32 CaseIndex = 0; CaseIndex < NumCases - 1; CaseIndex++)
	{
		int32 NumInstructionsToEnd = WorkData.VM->GetByteCode().GetNumInstructions() - JumpToEndInstructions[CaseIndex];
		WorkData.VM->GetByteCode().GetOpAt<FRigVMJumpOp>(JumpToEndBytes[CaseIndex]).InstructionIndex = NumInstructionsToEnd;
	}
}

FString URigVMCompiler::GetPinHash(URigVMPin* InPin, const FRigVMVarExprAST* InVarExpr, bool bIsDebugValue)
{
	FString Prefix = bIsDebugValue ? TEXT("DebugWatch:") : TEXT("");
	FString Suffix;

	if (InPin->IsExecuteContext())
	{
		return TEXT("ExecuteContext!");
	}

	URigVMNode* Node = InPin->GetNode();

	bool bIsExecutePin = false;
	bool bIsLiteral = false;
	bool bIsParameter = false;
	bool bIsVariable = false;

	if (InVarExpr != nullptr && !bIsDebugValue)
	{
		bIsExecutePin = InPin->IsExecuteContext();
		bIsLiteral = InVarExpr->GetType() == FRigVMExprAST::EType::Literal;

		bIsParameter = Cast<URigVMParameterNode>(Node) != nullptr;
		bIsVariable = Cast<URigVMVariableNode>(Node) != nullptr;

		// determine if this is an initialization for an IO pin
		if (!bIsLiteral && !bIsParameter && !bIsVariable &&
			!bIsExecutePin && (InPin->GetDirection() == ERigVMPinDirection::IO ||
			(InPin->GetDirection() == ERigVMPinDirection::Input && InPin->GetSourceLinks().Num() == 0)))
		{
			Suffix = TEXT("::IO");
		}
	}

	if (URigVMParameterNode* ParameterNode = Cast<URigVMParameterNode>(Node))
	{
		if (InPin->GetName() == TEXT("Value") && !bIsLiteral)
		{
			return FString::Printf(TEXT("%sParameter::%s%s"), *Prefix, *ParameterNode->GetParameterName().ToString(), *Suffix);
		}
	}
	else if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
	{
		if (InPin->GetName() == TEXT("Value") && !bIsLiteral && !bIsDebugValue)
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

	const URigVMPin::FDefaultValueOverride& DefaultValueOverride = InVarExpr->GetParser()->GetPinDefaultOverrides();
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
				TSharedPtr<FStructOnScope> DefaultStruct = WorkData.DefaultStructs.Last();
				FRigVMStruct* VMStruct = (FRigVMStruct *)DefaultStruct->GetStructMemory();

				int32 DesiredArraySize = VMStruct->GetArraySize(Pin->GetFName(), WorkData.RigVMUserData);

				DefaultValues = URigVMController::SplitDefaultValue(Pin->GetDefaultValue(DefaultValueOverride));

				if (DefaultValues.Num() != DesiredArraySize)
				{
					FString DefaultValue;
					if (Pin->GetArraySize() > 0)
					{
						DefaultValue = Pin->GetSubPins()[0]->GetDefaultValue(DefaultValueOverride);
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
				DefaultValues = URigVMController::SplitDefaultValue(Pin->GetDefaultValue(DefaultValueOverride));
			}

			while (DefaultValues.Num() < Pin->GetSubPins().Num())
			{
				DefaultValues.Add(FString());
			}
		}
		else if (URigVMEnumNode* EnumNode = Cast<URigVMEnumNode>(Pin->GetNode()))
		{
			FString EnumValueStr = EnumNode->GetDefaultValue(DefaultValueOverride);
			if (UEnum* Enum = EnumNode->GetEnum())
			{
				DefaultValues.Add(FString::FromInt((int32)Enum->GetValueByNameString(EnumValueStr)));
			}
			else
			{
				DefaultValues.Add(FString::FromInt(0));
			}
		}
		else
		{
			DefaultValues.Add(Pin->GetDefaultValue(DefaultValueOverride));
		}

		UScriptStruct* ScriptStruct = Pin->GetScriptStruct();
		if (ScriptStruct == nullptr)
		{
			ScriptStruct = GetScriptStructForCPPType(BaseCPPType);
		}

		FRigVMMemoryContainer& Memory = bIsLiteral ? WorkData.VM->GetLiteralMemory() : WorkData.VM->GetWorkMemory();

		bool bRegisterIsMultiUse = Settings.ConsolidateWorkRegisters;
		if (bRegisterIsMultiUse)
		{
			bRegisterIsMultiUse = !bIsLiteral && !bIsDebugValue;
		}
		if (bRegisterIsMultiUse)
		{
			bRegisterIsMultiUse = Pin->GetDirection() == ERigVMPinDirection::Output || Pin->GetDirection() == ERigVMPinDirection::IO;
		}
		if (bRegisterIsMultiUse)
		{
			bRegisterIsMultiUse = !bIsExecutePin && !Pin->IsDynamicArray();
		}
		if (bRegisterIsMultiUse)
		{
			if (URigVMStructNode* StructNode = Cast<URigVMStructNode>(Pin->GetNode()))
			{
				if (StructNode->IsLoopNode())
				{
					bRegisterIsMultiUse = false;
				}
			}
		}

		// look for a register to reuse
		if (bRegisterIsMultiUse)
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
			int32 NumSlices = 1;
			if (Pin->GetDirection() == ERigVMPinDirection::Hidden)
			{
				NumSlices = Pin->GetNumSlices(WorkData.RigVMUserData);
				ensure(NumSlices >= 1);
			}

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

				int32 Register = Memory.AddRegisterArray(!Pin->IsDynamicArray() && !bIsDebugValue, RegisterName, ScriptStruct->GetStructureSize(), DefaultValues.Num(), Pin->IsArray(), Data.GetData(), NumSlices, ERigVMRegisterType::Struct, ScriptStruct);
				ScriptStruct->DestroyStruct(Data.GetData(), DefaultValues.Num());

				Operand = Memory.GetOperand(Register);
			}
			else if (UEnum* Enum = Pin->GetEnum())
			{
				FRigVMByteArray Values;
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

				int32 Register = Memory.AddRegisterArray<uint8>(!Pin->IsDynamicArray() && !bIsDebugValue, RegisterName, Values.Num(), Pin->IsArray(), Values.GetData(), NumSlices, ERigVMRegisterType::Plain, nullptr);
				Operand = Memory.GetOperand(Register);
			}
			else if (BaseCPPType == TEXT("bool"))
			{
				TArray<bool> Values;
				for (FString DefaultValue : DefaultValues)
				{
					Values.Add((DefaultValue == TEXT("True")) || (DefaultValue == TEXT("true")) || (DefaultValue == TEXT("1")));
				}
				int32 Register = Memory.AddRegisterArray<bool>(!Pin->IsDynamicArray() && !bIsDebugValue, RegisterName, Values.Num(), Pin->IsArray(), (uint8*)Values.GetData(), NumSlices, ERigVMRegisterType::Plain, nullptr);
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
				int32 Register = Memory.AddRegisterArray<int32>(!Pin->IsDynamicArray() && !bIsDebugValue, RegisterName, Values.Num(), Pin->IsArray(), (uint8*)Values.GetData(), NumSlices, ERigVMRegisterType::Plain, nullptr);
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
				int32 Register = Memory.AddRegisterArray<float>(!Pin->IsDynamicArray() && !bIsDebugValue, RegisterName, Values.Num(), Pin->IsArray(), (uint8*)Values.GetData(), NumSlices, ERigVMRegisterType::Plain, nullptr);
				Operand = Memory.GetOperand(Register);
			}
			else if (BaseCPPType == TEXT("FName"))
			{
				TArray<FName> Values;
				for (FString DefaultValue : DefaultValues)
				{
					Values.Add(*DefaultValue);
				}
				int32 Register = Memory.AddRegisterArray<FName>(!Pin->IsDynamicArray() && !bIsDebugValue, RegisterName, Values.Num(), Pin->IsArray(), (uint8*)Values.GetData(), NumSlices, ERigVMRegisterType::Name, nullptr);
				Operand = Memory.GetOperand(Register);
			}
			else if (BaseCPPType == TEXT("FString"))
			{
				int32 Register = Memory.AddRegisterArray<FString>(!Pin->IsDynamicArray() && !bIsDebugValue, RegisterName, DefaultValues.Num(), Pin->IsArray(), (uint8*)DefaultValues.GetData(), NumSlices, ERigVMRegisterType::String, nullptr);
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
