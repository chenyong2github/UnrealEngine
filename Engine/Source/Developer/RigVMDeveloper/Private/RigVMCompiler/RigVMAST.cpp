// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCompiler/RigVMAST.h"
#include "RigVMCompiler/RigVMCompiler.h"
#include "RigVMModel/Nodes/RigVMStructNode.h"
#include "RigVMModel/Nodes/RigVMParameterNode.h"
#include "RigVMModel/Nodes/RigVMVariableNode.h"
#include "RigVMModel/Nodes/RigVMCommentNode.h"
#include "RigVMModel/Nodes/RigVMRerouteNode.h"
#include "RigVMModel/Nodes/RigVMBranchNode.h"
#include "RigVMModel/Nodes/RigVMIfNode.h"
#include "RigVMModel/Nodes/RigVMSelectNode.h"
#include "RigVMModel/Nodes/RigVMEnumNode.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMController.h"
#include "RigVMCore/RigVMExecuteContext.h"
#include "Stats/StatsHierarchical.h"

FRigVMExprAST::FRigVMExprAST(EType InType, UObject* InSubject)
	: Name(NAME_None)
	, Type(InType)
	, Index(INDEX_NONE)
{
}

FName FRigVMExprAST::GetTypeName() const
{
	switch (GetType())
	{
		case EType::Block:
		{
			return TEXT("[.Block.]");
		}
		case EType::Entry:
		{
			return TEXT("[.Entry.]");
		}
		case EType::CallExtern:
		{
			return TEXT("[.Call..]");
		}
		case EType::NoOp:
		{
			return TEXT("[.NoOp..]");
		}
		case EType::Var:
		{
			return TEXT("[.Var...]");
		}
		case EType::Literal:
		{
			return TEXT("[Literal]");
		}
		case EType::Assign:
		{
			return TEXT("[.Assign]");
		}
		case EType::Copy:
		{
			return TEXT("[.Copy..]");
		}
		case EType::CachedValue:
		{
			return TEXT("[.Cache.]");
		}
		case EType::Exit:
		{
			return TEXT("[.Exit..]");
		}
		case EType::Branch:
		{
			return TEXT("[Branch.]");
		}
		case EType::If:
		{
			return TEXT("[..If...]");
		}
		case EType::Select:
		{
			return TEXT("[Select.]");
		}
		case EType::Invalid:
		{
			return TEXT("[Invalid]");
		}
		default:
		{
			ensure(false);
		}
	}
	return NAME_None;
}

const FRigVMExprAST* FRigVMExprAST::GetParent() const
{
	if (Parents.Num() > 0)
	{
		return ParentAt(0);
	}
	return nullptr;
}

const FRigVMExprAST* FRigVMExprAST::GetFirstParentOfType(EType InExprType) const
{
	for(const FRigVMExprAST* Parent : Parents)
	{
		if (Parent->IsA(InExprType))
		{
			return Parent;
		}
	}
	for (const FRigVMExprAST* Parent : Parents)
	{
		if (const FRigVMExprAST* GrandParent = Parent->GetFirstParentOfType(InExprType))
		{
			return GrandParent;
		}
	}
	return nullptr;
}

const FRigVMExprAST* FRigVMExprAST::GetFirstChildOfType(EType InExprType) const
{
	for (const FRigVMExprAST* Child : Children)
	{
		if (Child->IsA(InExprType))
		{
			return Child;
		}
	}
	for (const FRigVMExprAST* Child : Children)
	{
		if (const FRigVMExprAST* GrandChild = Child->GetFirstChildOfType(InExprType))
		{
			return GrandChild;
		}
	}
	return nullptr;
}

const FRigVMBlockExprAST* FRigVMExprAST::GetBlock() const
{
	if (Parents.Num() == 0)
	{
		if (IsA(EType::Block))
		{
			return To<FRigVMBlockExprAST>();
		}
		return ParserPtr->GetObsoleteBlock();
	}

	const FRigVMExprAST* Parent = GetParent();
	if (Parent->IsA(EType::Block))
	{
		return Parent->To<FRigVMBlockExprAST>();
	}

	return Parent->GetBlock();
}

const FRigVMBlockExprAST* FRigVMExprAST::GetRootBlock() const
{
	const FRigVMBlockExprAST* Block = GetBlock();

	if (IsA(EType::Block))
	{
		if (Block && NumParents() > 0)
		{
			return Block->GetRootBlock();
		}
		return To<FRigVMBlockExprAST>();
	}

	if (Block)
	{
		return Block->GetRootBlock();
	}

	return nullptr;
}

int32 FRigVMExprAST::GetMinChildIndexWithinParent(const FRigVMExprAST* InParentExpr) const
{
	int32 MinIndex = INDEX_NONE;

	for (const FRigVMExprAST* Parent : Parents)
	{
		int32 ChildIndex = INDEX_NONE;

		if (Parent == InParentExpr)
		{
			Parent->Children.Find((FRigVMExprAST*)this, ChildIndex);
		}
		else
		{
			ChildIndex = Parent->GetMinChildIndexWithinParent(InParentExpr);
		}

		if (ChildIndex < MinIndex || MinIndex == INDEX_NONE)
		{
			MinIndex = ChildIndex;
		}
	}

	return MinIndex;
}

void FRigVMExprAST::AddParent(FRigVMExprAST* InParent)
{
	ensure(InParent != this);
	if (Parents.Contains(InParent))
	{
		return;
	}

	InParent->Children.Add(this);
	Parents.Add(InParent);
}

void FRigVMExprAST::RemoveParent(FRigVMExprAST* InParent)
{
	if (Parents.Remove(InParent) > 0)
	{
		InParent->Children.Remove(this);
	}
}

void FRigVMExprAST::RemoveChild(FRigVMExprAST* InChild)
{
	InChild->RemoveParent(this);
}

void FRigVMExprAST::ReplaceParent(FRigVMExprAST* InCurrentParent, FRigVMExprAST* InNewParent)
{
	for (int32 ParentIndex = 0; ParentIndex < Parents.Num(); ParentIndex++)
	{
		if (Parents[ParentIndex] == InCurrentParent)
		{
			Parents[ParentIndex] = InNewParent;
			InCurrentParent->Children.Remove(this);
			InNewParent->Children.Add(this);
		}
	}
}

void FRigVMExprAST::ReplaceChild(FRigVMExprAST* InCurrentChild, FRigVMExprAST* InNewChild)
{
	for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ChildIndex++)
	{
		if (Children[ChildIndex] == InCurrentChild)
		{
			Children[ChildIndex] = InNewChild;
			InCurrentChild->Parents.Remove(this);
			InNewChild->Parents.Add(this);
		}
	}
}

void FRigVMExprAST::ReplaceBy(FRigVMExprAST* InReplacement)
{
	TArray<FRigVMExprAST*> PreviousParents;
	PreviousParents.Append(Parents);

	for (FRigVMExprAST* PreviousParent : PreviousParents)
	{
		PreviousParent->ReplaceChild(this, InReplacement);
	}
}

bool FRigVMExprAST::IsConstant() const
{
	for (FRigVMExprAST* ChildExpr : Children)
	{
		if (!ChildExpr->IsConstant())
		{
			return false;
		}
	}
	return true;
}

FString FRigVMExprAST::DumpText(const FString& InPrefix) const
{
	FString Result;
	if (Name.IsNone())
	{
		Result = FString::Printf(TEXT("%s%s"), *InPrefix, *GetTypeName().ToString());
	}
	else
	{
		Result = FString::Printf(TEXT("%s%s %s"), *InPrefix, *GetTypeName().ToString(), *Name.ToString());
	}

	if (Children.Num() > 0)
	{
		FString Prefix = InPrefix;
		if (Prefix.IsEmpty())
		{
			Prefix = TEXT("-- ");
		}
		else
		{
			Prefix = TEXT("---") + Prefix;
		}
		for (FRigVMExprAST* Child : Children)
		{
			Result += TEXT("\n") + Child->DumpText(Prefix);
		}
	}
	return Result;
}

FString FRigVMExprAST::DumpDot(TArray<bool>& OutExpressionDefined, const FString& InPrefix) const
{
	FString Prefix = InPrefix;

	FString Result;
	bool bWasDefined = true;
	if (!OutExpressionDefined[GetIndex()])
	{
		bWasDefined = false;

		FString Label = GetName().ToString();
		FString AdditionalNodeSettings;
		switch (GetType())
		{
			case EType::Literal:
			{
				Label = FString::Printf(TEXT("%s(Literal)"), *To<FRigVMLiteralExprAST>()->GetPin()->GetName());
				break;
			}
			case EType::Var:
			{
				if (To<FRigVMVarExprAST>()->IsGraphParameter())
				{
					URigVMParameterNode* ParameterNode = Cast<URigVMParameterNode>(To<FRigVMVarExprAST>()->GetPin()->GetNode());
					check(ParameterNode);
					Label = FString::Printf(TEXT("Param %s"), *ParameterNode->GetParameterName().ToString());
				}
				else if (To<FRigVMVarExprAST>()->IsGraphVariable())
				{
					URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(To<FRigVMVarExprAST>()->GetPin()->GetNode());
					check(VariableNode);
					Label = FString::Printf(TEXT("Variable %s"), *VariableNode->GetVariableName().ToString());
				}
				else if (To<FRigVMVarExprAST>()->IsEnumValue())
				{
					URigVMEnumNode* EnumNode = Cast<URigVMEnumNode>(To<FRigVMVarExprAST>()->GetPin()->GetNode());
					check(EnumNode);
					Label = FString::Printf(TEXT("Enum %s"), *EnumNode->GetCPPType());
				}
				else
				{
					Label = To<FRigVMVarExprAST>()->GetPin()->GetName();
				}
				if (To<FRigVMVarExprAST>()->IsExecuteContext())
				{
					AdditionalNodeSettings += TEXT(", shape = cds");
				}
				break;
			}
			case EType::Block:
			{
				if (GetParent() == nullptr)
				{
					Label = TEXT("Unused");
					Result += FString::Printf(TEXT("\n%ssubgraph unused_%d {"), *Prefix, GetIndex());
					Prefix += TEXT("  ");
				}
				else
				{
					Label = TEXT("Block");
				}
				break;
			}
			case EType::Assign:
			{
				Label = TEXT("=");
				break;
			}
			case EType::Copy:
			{
				Label = TEXT("Copy");
				break;
			}
			case EType::CachedValue:
			{
				Label = TEXT("Cache");
				break;
			}
			case EType::CallExtern:
			{
				if (URigVMStructNode* Node = Cast<URigVMStructNode>(To<FRigVMCallExternExprAST>()->GetNode()))
				{
					Label = Node->GetScriptStruct()->GetName();
				}
				break;
			}
			case EType::NoOp:
			{
				Label = TEXT("NoOp");
				break;
			}
			case EType::Exit:
			{
				Label = TEXT("Exit");
				break;
			}
			case EType::Entry:
			{
				Result += FString::Printf(TEXT("\n%ssubgraph %s_%d {"), *Prefix, *GetName().ToString(), GetIndex());
				Prefix += TEXT("  ");
				break;
			}
			default:
			{
				break;
			}
		}

		if (!Label.IsEmpty())
		{
			Result += FString::Printf(TEXT("\n%snode_%d [label = \"%s\"%s];"), *Prefix, GetIndex(), *Label, *AdditionalNodeSettings);
		}

		switch (GetType())
		{
			case EType::Entry:
			case EType::Exit:
			case EType::Branch:
			case EType::Block:
			{
				Result += FString::Printf(TEXT("\n%snode_%d [shape = Mdiamond];"), *Prefix, GetIndex());
				break;
			}
			case EType::Assign:
			case EType::Copy:
			case EType::CallExtern:
			case EType::If:
			case EType::Select:
			case EType::NoOp:
			{
				Result += FString::Printf(TEXT("\n%snode_%d [shape = box];"), *Prefix, GetIndex());
				break;
			}
			default:
			{
				break;
			}
		}

	}

	for (FRigVMExprAST* Child : Children)
	{
		Result += Child->DumpDot(OutExpressionDefined, Prefix);
		if(!bWasDefined)
		{
			Result += FString::Printf(TEXT("\n%snode_%d -> node_%d;"), *Prefix, GetIndex(), Child->GetIndex());
		}
	}

	if (!OutExpressionDefined[GetIndex()])
	{
		switch (GetType())
		{
			case EType::Block:
			{
				if (GetParent() == nullptr)
				{
					Prefix = Prefix.LeftChop(2);
					Result += FString::Printf(TEXT("\n%s}"), *Prefix, *GetName().ToString(), GetIndex());
				}
				break;
			}
			case EType::Entry:
			{
				Prefix = Prefix.LeftChop(2);
				Result += FString::Printf(TEXT("\n%s}"), *Prefix, *GetName().ToString(), GetIndex());
				break;
			}
			default:
			{
				break;
			}
		}
	}

	OutExpressionDefined[GetIndex()] = true;

	return Result;
}

bool FRigVMBlockExprAST::ShouldExecute() const
{
	return ContainsEntry();
}

bool FRigVMBlockExprAST::ContainsEntry() const
{
	if (IsA(FRigVMExprAST::EType::Entry))
	{
		return true;
	}
	for (FRigVMExprAST* Expression : *this)
	{
		if (Expression->IsA(EType::Entry))
		{
			return true;
		}
	}
	return false;
}

bool FRigVMBlockExprAST::Contains(const FRigVMExprAST* InExpression) const
{
	if (InExpression == this)
	{
		return true;
	}

	for (int32 ParentIndex = 0; ParentIndex < InExpression->NumParents(); ParentIndex++)
	{
		const FRigVMExprAST* ParentExpr = InExpression->ParentAt(ParentIndex);
		if (Contains(ParentExpr))
		{
			return true;
		}
	}

	return false;
}

bool FRigVMNodeExprAST::IsConstant() const
{
	if (URigVMNode* CurrentNode = GetNode())
	{
		if (CurrentNode->IsDefinedAsConstant())
		{
			return true;
		}
		else if (CurrentNode->IsDefinedAsVarying())
		{
			return false;
		}
	}
	return FRigVMExprAST::IsConstant();
}

FRigVMNodeExprAST::FRigVMNodeExprAST(EType InType, UObject* InSubject /*= nullptr*/) 
	: FRigVMBlockExprAST(InType)
	, Node(Cast<URigVMNode>(InSubject))
{
	check(Node);
}

FName FRigVMEntryExprAST::GetEventName() const
{
	if (URigVMNode* EventNode = GetNode())
	{
		return EventNode->GetEventName();
	}
	return NAME_None;
}

bool FRigVMVarExprAST::IsConstant() const
{
	if (GetPin()->IsExecuteContext())
	{
		return false;
	}

	if (GetPin()->IsDefinedAsConstant())
	{
		return true;
	}

	if (SupportsSoftLinks())
	{
		return false;
	}

	ERigVMPinDirection Direction = GetPin()->GetDirection();
	if (Direction == ERigVMPinDirection::Hidden)
	{
		if (Cast<URigVMVariableNode>(GetPin()->GetNode()))
		{
			if (GetPin()->GetName() == URigVMVariableNode::VariableName)
			{
				return true;
			}
		}
		return false;
	}

	if (GetPin()->GetDirection() == ERigVMPinDirection::IO ||
		GetPin()->GetDirection() == ERigVMPinDirection::Output)
	{
		if (GetPin()->GetNode()->IsDefinedAsVarying())
		{
			return false;
		}
	}

	return FRigVMExprAST::IsConstant();
}

FString FRigVMVarExprAST::GetCPPType() const
{
	return Pin->GetCPPType();
}

UObject* FRigVMVarExprAST::GetCPPTypeObject() const
{
	return Pin->GetCPPTypeObject();
}

ERigVMPinDirection FRigVMVarExprAST::GetPinDirection() const
{
	return Pin->GetDirection();
}

FString FRigVMVarExprAST::GetDefaultValue() const
{
	return Pin->GetDefaultValue(GetParser()->GetPinDefaultOverrides());
}

bool FRigVMVarExprAST::IsExecuteContext() const
{
	return Pin->IsExecuteContext();
}

bool FRigVMVarExprAST::IsGraphParameter() const
{
	if (Cast<URigVMParameterNode>(Pin->GetNode()))
	{
		return Pin->GetName() == TEXT("Value");
	}
	return false;
}

bool FRigVMVarExprAST::IsGraphVariable() const
{
	if (Cast<URigVMVariableNode>(Pin->GetNode()))
	{
		return Pin->GetName() == URigVMVariableNode::ValueName;
	}
	return false;
}

bool FRigVMVarExprAST::IsEnumValue() const
{
	if (Cast<URigVMEnumNode>(Pin->GetNode()))
	{
		return Pin->GetName() == TEXT("EnumIndex");
	}
	return false;
}

bool FRigVMVarExprAST::SupportsSoftLinks() const
{
	if (URigVMStructNode* StructNode = Cast<URigVMStructNode>(Pin->GetNode()))
	{
		if (StructNode->IsLoopNode())
		{
			if (Pin->GetFName() != FRigVMStruct::ExecuteContextName &&
				Pin->GetFName() != FRigVMStruct::ForLoopCompletedPinName)
			{
				return true;
			}
		}
	}
	return false;
}


bool FRigVMBranchExprAST::IsConstant() const
{
	if (IsAlwaysTrue())
	{
		return GetTrueExpr()->IsConstant();
	}
	else if(IsAlwaysFalse())
	{
		return GetFalseExpr()->IsConstant();
	}
	return FRigVMNodeExprAST::IsConstant();
}

bool FRigVMBranchExprAST::IsAlwaysTrue() const
{
	const FRigVMVarExprAST* ConditionExpr = GetConditionExpr();
	if (ConditionExpr->IsA(EType::Literal))
	{
		const FString& PinDefaultValue = ConditionExpr->GetDefaultValue();
		return PinDefaultValue == TEXT("True");
	}
	return false;
}

bool FRigVMBranchExprAST::IsAlwaysFalse() const
{
	const FRigVMVarExprAST* ConditionExpr = GetConditionExpr();
	if (ConditionExpr->IsA(EType::Literal))
	{
		const FString& PinDefaultValue = ConditionExpr->GetDefaultValue();
		return PinDefaultValue == TEXT("False") || PinDefaultValue.IsEmpty();
	}
	return false;
}

bool FRigVMIfExprAST::IsConstant() const
{
	if (IsAlwaysTrue())
	{
		return GetTrueExpr()->IsConstant();
	}
	else if (IsAlwaysFalse())
	{
		return GetFalseExpr()->IsConstant();
	}
	return FRigVMNodeExprAST::IsConstant();
}

bool FRigVMIfExprAST::IsAlwaysTrue() const
{
	const FRigVMVarExprAST* ConditionExpr = GetConditionExpr();
	if (ConditionExpr->IsA(EType::Literal))
	{
		const FString& PinDefaultValue = ConditionExpr->GetDefaultValue();
		return PinDefaultValue == TEXT("True");
	}
	return false;
}

bool FRigVMIfExprAST::IsAlwaysFalse() const
{
	const FRigVMVarExprAST* ConditionExpr = GetConditionExpr();
	if (ConditionExpr->IsA(EType::Literal))
	{
		const FString& PinDefaultValue = ConditionExpr->GetDefaultValue();
		return PinDefaultValue == TEXT("False") || PinDefaultValue.IsEmpty();
	}
	return false;
}

bool FRigVMSelectExprAST::IsConstant() const
{
	int32 ConstantCaseIndex = GetConstantValueIndex();
	if (ConstantCaseIndex != INDEX_NONE)
	{
		return GetValueExpr(ConstantCaseIndex)->IsConstant();
	}
	return FRigVMNodeExprAST::IsConstant();
}

int32 FRigVMSelectExprAST::GetConstantValueIndex() const
{
	const FRigVMVarExprAST* IndexExpr = GetIndexExpr();
	if (IndexExpr->IsA(EType::Literal))
	{
		int32 NumCases = NumValues();
		if (NumCases == 0)
		{
			return INDEX_NONE;
		}

		const FString& PinDefaultValue = IndexExpr->GetDefaultValue();
		int32 CaseIndex = 0;
		if (!PinDefaultValue.IsEmpty())
		{
			CaseIndex = FCString::Atoi(*PinDefaultValue);
		}

		return FMath::Clamp<int32>(CaseIndex, 0, NumCases - 1);
	}
	return INDEX_NONE;
}

int32 FRigVMSelectExprAST::NumValues() const
{
	return GetNode()->FindPin(URigVMSelectNode::ValueName)->GetArraySize();
}

const FRigVMVarExprAST* FRigVMCallExternExprAST::FindVarWithPinName(const FName& InPinName) const
{
	for (int32 ChildIndex = 0; ChildIndex < NumChildren(); ChildIndex++)
	{
		const FRigVMExprAST* Child = ChildAt(ChildIndex);
		if (Child->IsA(FRigVMExprAST::Var))
		{
			const FRigVMVarExprAST* VarExpr = Child->To<FRigVMVarExprAST>();
			if (VarExpr->GetPin()->GetFName() == InPinName)
			{
				return VarExpr;
			}
		}
	}
	return nullptr;
}

FRigVMParserAST::FRigVMParserAST(URigVMGraph* InGraph, URigVMController* InController, const FRigVMParserASTSettings& InSettings, const TArray<FRigVMExternalVariable>& InExternalVariables, const TArray<FRigVMUserDataArray>& InRigVMUserData)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	ObsoleteBlock = nullptr;
	LastCycleCheckExpr = nullptr;

	const TArray<URigVMNode*> Nodes = InGraph->GetNodes();
	for (URigVMNode* Node : Nodes)
	{
		if(Node->IsEvent())
		{
			TraverseMutableNode(Node, nullptr);
		}
	}

	// traverse all remaining mutable nodes,
	// followed by a pass for all remaining non-mutable nodes
	for (int32 PassIndex = 0; PassIndex < 2; PassIndex++)
	{
		const bool bTraverseMutable = PassIndex == 0;
		for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); NodeIndex++)
		{
			if (const int32* ExprIndex = NodeExpressionIndex.Find(Nodes[NodeIndex]))
			{
				if (*ExprIndex != INDEX_NONE)
				{
					continue;
				}
			}

			if (Nodes[NodeIndex]->IsMutable() == bTraverseMutable)
			{
				if (bTraverseMutable)
				{
					TraverseMutableNode(Nodes[NodeIndex], GetObsoleteBlock());
				}
				else
				{
					TraverseNode(Nodes[NodeIndex], GetObsoleteBlock());
				}
			}
		}
	}

	FoldEntries();
	InjectExitsToEntries();

	if (InSettings.bFoldReroutes || InSettings.bFoldAssignments)
	{
		FoldNoOps();
	}

	// keep folding constant branches and values while we can
	bool bContinueToFoldConstantBranches = InSettings.bFoldConstantBranches;
	while (bContinueToFoldConstantBranches)
	{
		bContinueToFoldConstantBranches = false;
		if (FoldConstantValuesToLiterals(InGraph, InController, InExternalVariables, InRigVMUserData))
		{
			bContinueToFoldConstantBranches = true;
		}
		if (FoldUnreachableBranches(InGraph))
		{
			bContinueToFoldConstantBranches = true;
		}
	}

	BubbleUpExpressions();

	if (InSettings.bFoldAssignments)
	{
		FoldAssignments();
	}

	if (InSettings.bFoldLiterals)
	{
		FoldLiterals();
	}
}

FRigVMParserAST::FRigVMParserAST(URigVMGraph* InGraph, const TArray<URigVMNode*>& InNodesToCompute)
{
	LastCycleCheckExpr = nullptr;

	FRigVMBlockExprAST* Block = MakeExpr<FRigVMBlockExprAST>(FRigVMExprAST::EType::Block);
	Block->Name = TEXT("NodesToCompute");
	RootExpressions.Add(Block);

	for (URigVMNode* Node : InNodesToCompute)
	{
		if (Node->IsEvent())
		{
			continue;
		}
		if (Node->IsMutable())
		{
			continue;
		}
		TraverseNode(Node, Block);
	}

	FRigVMExprAST* ExitExpr = MakeExpr<FRigVMExitExprAST>();
	ExitExpr->AddParent(Block);
}

FRigVMParserAST::~FRigVMParserAST()
{
	for (FRigVMExprAST* Expression : Expressions)
	{
		delete(Expression);
	}
	Expressions.Empty();

	// root expressions are a subset of the
	// expressions array, so no cleanup necessary
	RootExpressions.Empty();
}

FRigVMExprAST* FRigVMParserAST::TraverseMutableNode(URigVMNode* InNode, FRigVMExprAST* InParentExpr)
{
	if (SubjectToExpression.Contains(InNode))
	{
		return SubjectToExpression.FindChecked(InNode);
	}

	FRigVMExprAST* NodeExpr = CreateExpressionForNode(InNode, InParentExpr);
	if (NodeExpr)
	{
		if (InParentExpr == nullptr)
		{
			InParentExpr = NodeExpr;
		}

		TraversePins(InNode, NodeExpr);

		for (URigVMPin* SourcePin : InNode->GetPins())
		{
			if (SourcePin->GetDirection() == ERigVMPinDirection::Output || SourcePin->GetDirection() == ERigVMPinDirection::IO)
			{
				if (SourcePin->IsExecuteContext())
				{
					bool bIsForLoop = false;
					if (URigVMStructNode* StructNode = Cast<URigVMStructNode>(InNode))
					{
						bIsForLoop = StructNode->IsLoopNode();
					}

					FRigVMExprAST* ParentExpr = InParentExpr;
					if (NodeExpr->IsA(FRigVMExprAST::Branch) || bIsForLoop)
					{
						if (FRigVMExprAST** PinExpr = SubjectToExpression.Find(SourcePin))
						{
							FRigVMBlockExprAST* BlockExpr = MakeExpr<FRigVMBlockExprAST>(FRigVMExprAST::EType::Block);
							BlockExpr->AddParent(*PinExpr);
							BlockExpr->Name = SourcePin->GetFName();
							ParentExpr = BlockExpr;
						}
					}

					TArray<URigVMPin*> TargetPins = SourcePin->GetLinkedTargetPins();
					for (URigVMPin* TargetPin : TargetPins)
					{
						TraverseMutableNode(TargetPin->GetNode(), ParentExpr);
					}
				}
			}
		}
	}

	return NodeExpr;
}

FRigVMExprAST* FRigVMParserAST::TraverseNode(URigVMNode* InNode, FRigVMExprAST* InParentExpr)
{
	if (Cast<URigVMCommentNode>(InNode))
	{
		return nullptr;
	
	}
	if (SubjectToExpression.Contains(InNode))
	{
		FRigVMExprAST* NodeExpr = SubjectToExpression.FindChecked(InNode);
		NodeExpr->AddParent(InParentExpr);
		return NodeExpr;
	}

	FRigVMExprAST* NodeExpr = CreateExpressionForNode(InNode, InParentExpr);
	if (NodeExpr)
	{
		TraversePins(InNode, NodeExpr);
	}

	return NodeExpr;
}

FRigVMExprAST* FRigVMParserAST::CreateExpressionForNode(URigVMNode* InNode, FRigVMExprAST* InParentExpr)
{
	FRigVMExprAST* NodeExpr = nullptr;
	if (InNode->IsEvent())
	{
		NodeExpr = MakeExpr<FRigVMEntryExprAST>(InNode);
		NodeExpr->Name = InNode->GetEventName();
	}
	else
	{
		if (Cast<URigVMRerouteNode>(InNode) ||
			Cast<URigVMParameterNode>(InNode) ||
			Cast<URigVMVariableNode>(InNode) ||
			Cast<URigVMEnumNode>(InNode))
		{
			NodeExpr = MakeExpr<FRigVMNoOpExprAST>(InNode);
		}
		else if (Cast<URigVMBranchNode>(InNode))
		{
			NodeExpr = MakeExpr<FRigVMBranchExprAST>(InNode);
		}
		else if (Cast<URigVMIfNode>(InNode))
		{
			NodeExpr = MakeExpr<FRigVMIfExprAST>(InNode);
		}
		else if (Cast<URigVMSelectNode>(InNode))
		{
			NodeExpr = MakeExpr<FRigVMSelectExprAST>(InNode);
		}
		else
		{
			NodeExpr = MakeExpr<FRigVMCallExternExprAST>(InNode);
		}
		NodeExpr->Name = InNode->GetFName();
	}

	if (InParentExpr != nullptr)
	{
		NodeExpr->AddParent(InParentExpr);
	}
	else
	{
		RootExpressions.Add(NodeExpr);
	}
	SubjectToExpression.Add(InNode, NodeExpr);
	NodeExpressionIndex.Add(InNode, NodeExpr->GetIndex());

	return NodeExpr;
}

TArray<FRigVMExprAST*> FRigVMParserAST::TraversePins(URigVMNode* InNode, FRigVMExprAST* InParentExpr)
{
	TArray<FRigVMExprAST*> PinExpressions;

	for (URigVMPin* Pin : InNode->GetPins())
	{
		if (Pin->GetDirection() == ERigVMPinDirection::Input &&
			InParentExpr->IsA(FRigVMExprAST::EType::Select))
		{
			if (Pin->GetName() == URigVMSelectNode::ValueName)
			{
				const TArray<URigVMPin*>& CasePins = Pin->GetSubPins();
				for (URigVMPin* CasePin : CasePins)
				{
					PinExpressions.Add(TraversePin(CasePin, InParentExpr));
				}
				continue;
			}
		}

		PinExpressions.Add(TraversePin(Pin, InParentExpr));
	}

	return PinExpressions;
}

FRigVMExprAST* FRigVMParserAST::TraversePin(URigVMPin* InPin, FRigVMExprAST* InParentExpr)
{
	ensure(!SubjectToExpression.Contains(InPin));

	TArray<URigVMLink*> SourceLinks = InPin->GetSourceLinks(true);

	FRigVMExprAST* PinExpr = nullptr;

	if (Cast<URigVMVariableNode>(InPin->GetNode()))
	{
		if (InPin->GetName() == URigVMVariableNode::VariableName)
		{
			return nullptr;
		}
	}
	else if (Cast<URigVMParameterNode>(InPin->GetNode()) ||
		Cast<URigVMEnumNode>(InPin->GetNode()))
	{
		if (InPin->GetDirection() == ERigVMPinDirection::Visible)
		{
			return nullptr;
		}
	}

	if ((InPin->GetDirection() == ERigVMPinDirection::Input ||
		InPin->GetDirection() == ERigVMPinDirection::Visible) &&
		SourceLinks.Num() == 0)
	{
		if (Cast<URigVMParameterNode>(InPin->GetNode()) ||
			Cast<URigVMVariableNode>(InPin->GetNode()))
		{
			PinExpr = MakeExpr<FRigVMVarExprAST>(FRigVMExprAST::EType::Var, InPin);
			FRigVMExprAST* PinLiteralExpr = MakeExpr<FRigVMLiteralExprAST>(InPin);
			PinLiteralExpr->Name = PinExpr->Name;
			FRigVMExprAST* PinCopyExpr = MakeExpr<FRigVMCopyExprAST>(InPin, InPin);
			PinCopyExpr->AddParent(PinExpr);
			PinLiteralExpr->AddParent(PinCopyExpr);
		}
		else
		{
			PinExpr = MakeExpr<FRigVMLiteralExprAST>(InPin);
		}
	}
	else if (Cast<URigVMEnumNode>(InPin->GetNode()))
	{
		PinExpr = MakeExpr<FRigVMLiteralExprAST>(InPin);
	}
	else
	{
		PinExpr = MakeExpr<FRigVMVarExprAST>(FRigVMExprAST::EType::Var, InPin);
	}

	PinExpr->AddParent(InParentExpr);
	PinExpr->Name = *InPin->GetPinPath();
	SubjectToExpression.Add(InPin, PinExpr);

	if (InPin->IsExecuteContext())
	{
		return PinExpr;
	}

	if ((InPin->GetDirection() == ERigVMPinDirection::IO ||
		InPin->GetDirection() == ERigVMPinDirection::Input) 
		&& !InPin->IsExecuteContext())
	{
		bool bHasSourceLinkToRoot = false;
		URigVMPin* RootPin = InPin->GetRootPin();
		for (URigVMLink* SourceLink : SourceLinks)
		{
			if (SourceLink->GetTargetPin() == RootPin)
			{
				bHasSourceLinkToRoot = true;
				break;
			}
		}

		if (!bHasSourceLinkToRoot && 
			InPin->GetSourceLinks(false).Num() == 0 &&
			(InPin->GetDirection() == ERigVMPinDirection::IO || SourceLinks.Num() > 0))
		{
			FRigVMLiteralExprAST* LiteralExpr = MakeExpr<FRigVMLiteralExprAST>(InPin);
			FRigVMCopyExprAST* LiteralCopyExpr = MakeExpr<FRigVMCopyExprAST>(InPin, InPin);
			LiteralCopyExpr->Name = *FString::Printf(TEXT("%s -> %s"), *InPin->GetPinPath(), *InPin->GetPinPath());
			LiteralCopyExpr->AddParent(PinExpr);
			LiteralExpr->AddParent(LiteralCopyExpr);
			LiteralExpr->Name = *InPin->GetPinPath();

			SubjectToExpression[InPin] = LiteralExpr;
		}
	}

	FRigVMExprAST* ParentExprForLinks = PinExpr;

	if ((InPin->GetDirection() == ERigVMPinDirection::IO || InPin->GetDirection() == ERigVMPinDirection::Input) &&
		(InParentExpr->IsA(FRigVMExprAST::If) || InParentExpr->IsA(FRigVMExprAST::Select)) &&
		SourceLinks.Num() > 0)
	{
		FRigVMBlockExprAST* BlockExpr = MakeExpr<FRigVMBlockExprAST>(FRigVMExprAST::EType::Block);
		BlockExpr->AddParent(PinExpr);
		BlockExpr->Name = InPin->GetFName();
		ParentExprForLinks = BlockExpr;
	}

	for (URigVMLink* SourceLink : SourceLinks)
	{
		TraverseLink(SourceLink, ParentExprForLinks);
	}

	return PinExpr;
}

FRigVMExprAST* FRigVMParserAST::TraverseLink(URigVMLink* InLink, FRigVMExprAST* InParentExpr)
{
	ensure(!SubjectToExpression.Contains(InLink));

	URigVMPin* SourcePin = InLink->GetSourcePin();
	URigVMPin* TargetPin = InLink->GetTargetPin();
	URigVMPin* SourceRootPin = SourcePin->GetRootPin();
	URigVMPin* TargetRootPin = TargetPin->GetRootPin();

	bool bRequiresCopy = SourceRootPin != SourcePin || TargetRootPin != TargetPin;
	if (!bRequiresCopy)
	{
		if(Cast<URigVMParameterNode>(TargetRootPin->GetNode()) ||
			Cast<URigVMVariableNode>(TargetRootPin->GetNode()))
		{
			bRequiresCopy = true;
		}
	}

	FRigVMAssignExprAST* AssignExpr = nullptr;
	if (bRequiresCopy)
	{
		AssignExpr = MakeExpr<FRigVMCopyExprAST>(SourcePin, TargetPin);
	}
	else
	{
		AssignExpr = MakeExpr<FRigVMAssignExprAST>(FRigVMExprAST::EType::Assign, SourcePin, TargetPin);
	}

	AssignExpr->Name = *InLink->GetPinPathRepresentation();
	AssignExpr->AddParent(InParentExpr);
	SubjectToExpression.Add(InLink, AssignExpr);

	FRigVMExprAST* NodeExpr = TraverseNode(SourcePin->GetNode(), AssignExpr);
	if (NodeExpr)
	{
		// if this is a copy expression - we should require the copy to use a ref instead
		if (NodeExpr->IsA(FRigVMExprAST::EType::CallExtern) ||
			NodeExpr->IsA(FRigVMExprAST::EType::If) ||
			NodeExpr->IsA(FRigVMExprAST::EType::Select))
		{
			for (FRigVMExprAST* ChildExpr : *NodeExpr)
			{
				if (ChildExpr->IsA(FRigVMExprAST::EType::Var))
				{
					FRigVMVarExprAST* VarExpr = ChildExpr->To<FRigVMVarExprAST>();
					if (VarExpr->GetPin() == SourceRootPin)
					{
						if (VarExpr->SupportsSoftLinks())
						{
							AssignExpr->ReplaceChild(NodeExpr, VarExpr);
							return AssignExpr;
						}

						FRigVMCachedValueExprAST* CacheExpr = nullptr;
						for (FRigVMExprAST* VarExprParent : VarExpr->Parents)
						{
							if (VarExprParent->IsA(FRigVMExprAST::EType::CachedValue))
							{
								CacheExpr = VarExprParent->To<FRigVMCachedValueExprAST>();
								break;
							}
						}

						if (CacheExpr == nullptr)
						{
							CacheExpr = MakeExpr<FRigVMCachedValueExprAST>();
							CacheExpr->Name = AssignExpr->GetName();
							VarExpr->AddParent(CacheExpr);
							NodeExpr->AddParent(CacheExpr);
						}

						AssignExpr->ReplaceChild(NodeExpr, CacheExpr);
						return AssignExpr;
					}
				}
			}
			checkNoEntry();
		}
	}

	return AssignExpr;
}

void FRigVMParserAST::FoldEntries()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	TArray<FRigVMExprAST*> FoldRootExpressions;
	TArray<FRigVMExprAST*> ExpressionsToRemove;
	TMap<FName, FRigVMEntryExprAST*> EntryByName;

	for (FRigVMExprAST* RootExpr : RootExpressions)
	{
		if (RootExpr->IsA(FRigVMExprAST::EType::Entry))
		{
			FRigVMEntryExprAST* Entry = RootExpr->To<FRigVMEntryExprAST>();
			if (EntryByName.Contains(Entry->GetEventName()))
			{
				FRigVMEntryExprAST* FoldEntry = EntryByName.FindChecked(Entry->GetEventName());

				// replace the original entry with a noop
				FRigVMNoOpExprAST* NoOpExpr = MakeExpr<FRigVMNoOpExprAST>(Entry->GetNode());
				NoOpExpr->AddParent(FoldEntry);
				NoOpExpr->Name = Entry->Name;
				SubjectToExpression.FindChecked(Entry->GetNode()) = NoOpExpr;

				TArray<FRigVMExprAST*> Children = Entry->Children; // copy since the loop changes the array
				for (FRigVMExprAST* ChildExpr : Children)
				{
					ChildExpr->RemoveParent(Entry);
					if (ChildExpr->IsA(FRigVMExprAST::Var))
					{
						if (ChildExpr->To<FRigVMVarExprAST>()->IsExecuteContext())
						{
							ExpressionsToRemove.AddUnique(ChildExpr);
							continue;
						}
					}
					ChildExpr->AddParent(FoldEntry);
				}
				ExpressionsToRemove.AddUnique(Entry);
			}
			else
			{
				FoldRootExpressions.Add(Entry);
				EntryByName.Add(Entry->GetEventName(), Entry);
			}
		}
		else
		{
			FoldRootExpressions.Add(RootExpr);
		}
	}

	RootExpressions = FoldRootExpressions;

	RemoveExpressions(ExpressionsToRemove);
}

void FRigVMParserAST::InjectExitsToEntries()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	for (FRigVMExprAST* RootExpr : RootExpressions)
	{
		if (RootExpr->IsA(FRigVMExprAST::EType::Entry))
		{
			bool bHasExit = false;
			if (RootExpr->Children.Num() > 0)
			{
				if (RootExpr->Children.Last()->IsA(FRigVMExprAST::EType::Exit))
				{
					bHasExit = true;
					break;
				}
			}

			if (!bHasExit)
			{
				FRigVMExprAST* ExitExpr = MakeExpr<FRigVMExitExprAST>();
				ExitExpr->AddParent(RootExpr);
			}
		}
	}
}

void FRigVMParserAST::BubbleUpExpressions()
{
	for (int32 ExpressionIndex = 0; ExpressionIndex < Expressions.Num(); ExpressionIndex++)
	{
		FRigVMExprAST* Expression = Expressions[ExpressionIndex];
		if (!Expression->IsA(FRigVMExprAST::CachedValue))
		{
			continue;
		}

		if (Expression->NumParents() < 2)
		{
			continue;
		}

		// collect all of the blocks this is in and make sure it's bubbled up before that
		TArray<FRigVMBlockExprAST*> Blocks;
		for (int32 ParentIndex = 0; ParentIndex < Expression->NumParents(); ParentIndex++)
		{
			const FRigVMExprAST* ParentExpression = Expression->ParentAt(ParentIndex);
			if (ParentExpression->IsA(FRigVMExprAST::Block))
			{
				Blocks.AddUnique((FRigVMBlockExprAST*)ParentExpression->To<FRigVMBlockExprAST>());
			}
			else
			{
				Blocks.AddUnique((FRigVMBlockExprAST*)ParentExpression->GetBlock());
			}
		}

		if (Blocks.Num() > 1)
		{
			// this expression is part of multiple blocks, and it needs to be bubbled up.
			// for this we'll walk up the block tree and find the first block which contains all of them
			TArray<FRigVMBlockExprAST*> BlockCandidates;
			BlockCandidates.Append(Blocks);
			FRigVMBlockExprAST* OuterBlock = nullptr;

			for (int32 BlockCandidateIndex = 0; BlockCandidateIndex < BlockCandidates.Num(); BlockCandidateIndex++)
			{
				FRigVMBlockExprAST* BlockCandidate = BlockCandidates[BlockCandidateIndex];

				bool bFoundCandidate = true;
				for (int32 BlockIndex = 0; BlockIndex < Blocks.Num(); BlockIndex++)
				{
					FRigVMBlockExprAST* Block = Blocks[BlockIndex];
					if (!BlockCandidate->Contains(Block))
					{
						bFoundCandidate = false;
						break;
					}
				}

				if (bFoundCandidate)
				{
					OuterBlock = BlockCandidate;
					break;
				}

				BlockCandidates.AddUnique((FRigVMBlockExprAST*)BlockCandidate->GetBlock());
			}

			// we found a block which contains all of our blocks.
			// we are now going to inject this block as the first parent
			// of the cached value, so that the traverser sees it earlier
			if (OuterBlock)
			{
				int32 ChildIndex = Expression->GetMinChildIndexWithinParent(OuterBlock);
				if (ChildIndex != INDEX_NONE)
				{
					OuterBlock->Children.Insert(Expression, ChildIndex);
					Expression->Parents.Insert(OuterBlock, 0);
				}
			}
		}
	}
}

void FRigVMParserAST::RefreshExprIndices()
{
	for (int32 Index = 0; Index < Expressions.Num(); Index++)
	{
		Expressions[Index]->Index = Index;
	}
}

void FRigVMParserAST::FoldNoOps()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	for (FRigVMExprAST* Expression : Expressions)
	{
		if (Expression->IsA(FRigVMExprAST::EType::NoOp))
		{
			if (URigVMNode* Node = Expression->To<FRigVMNoOpExprAST>()->GetNode())
			{
				if (URigVMParameterNode* ParameterNode = Cast<URigVMParameterNode>(Node))
				{
					if (!ParameterNode->IsInput())
					{
						continue;
					}
				}
				if(URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
				{
					if (!VariableNode->IsGetter())
					{
						continue;
					}
				}
			}
			// copy since we are changing the content during iteration below
			TArray<FRigVMExprAST*> Children = Expression->Children;
			TArray<FRigVMExprAST*> Parents = Expression->Parents;

			for (FRigVMExprAST* Parent : Parents)
			{
				Expression->RemoveParent(Parent);
			}

			for (FRigVMExprAST* Child : Children)
			{
				Child->RemoveParent(Expression);
				for (FRigVMExprAST* Parent : Parents)
				{
					Child->AddParent(Parent);
				}
			}
		}
	}
}

void FRigVMParserAST::FoldAssignments()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	TArray<FRigVMExprAST*> ExpressionsToRemove;

	// first - Fold all assignment chains
	for (FRigVMExprAST* Expression : Expressions)
	{
		if (Expression->Parents.Num() == 0)
		{
			continue;
		}

		if (Expression->GetType() != FRigVMExprAST::EType::Assign)
		{
			continue;
		}

		FRigVMAssignExprAST* AssignExpr = Expression->To<FRigVMAssignExprAST>();
		ensure(AssignExpr->Parents.Num() == 1);
		ensure(AssignExpr->Children.Num() == 1);

		// non-input pins on anything but a reroute node should be skipped
		if (AssignExpr->GetTargetPin()->GetDirection() != ERigVMPinDirection::Input &&
			Cast<URigVMRerouteNode>(AssignExpr->GetTargetPin()->GetNode()) == nullptr)
		{
			continue;
		}

		// if this node is a loop node - let's skip the folding
		if (URigVMStructNode* StructNode = Cast<URigVMStructNode>(AssignExpr->GetTargetPin()->GetNode()))
		{
			if (StructNode->IsLoopNode())
			{
				continue;
			}
		}

		// if this node is a variable node and the pin requires a watch... skip this
		if (Cast<URigVMVariableNode>(AssignExpr->GetSourcePin()->GetNode()))
		{
			if(AssignExpr->GetSourcePin()->RequiresWatch())
			{
				continue;
			}
		}

		FRigVMExprAST* Parent = AssignExpr->Parents[0];
		if (!Parent->IsA(FRigVMExprAST::EType::Var))
		{
			continue;
		}

		FRigVMExprAST* Child = AssignExpr->Children[0];
		AssignExpr->RemoveParent(Parent);
		Child->RemoveParent(AssignExpr);

		TArray<FRigVMExprAST*> GrandParents = Parent->Parents;
		for (FRigVMExprAST* GrandParent : GrandParents)
		{
			GrandParent->ReplaceChild(Parent, Child);
			if (GrandParent->IsA(FRigVMExprAST::EType::Assign))
			{
				FRigVMAssignExprAST* GrandParentAssign = GrandParent->To<FRigVMAssignExprAST>();
				GrandParentAssign->SourcePin = AssignExpr->SourcePin;
				GrandParentAssign->Name = *FString::Printf(TEXT("%s -> %s"), *GrandParentAssign->SourcePin->GetPinPath(), *GrandParentAssign->TargetPin->GetPinPath());
			}
		}

		ExpressionsToRemove.AddUnique(AssignExpr);
		if (Parent->Parents.Num() == 0)
		{
			ExpressionsToRemove.AddUnique(Parent);
		}
	}

	RemoveExpressions(ExpressionsToRemove);
}

bool FRigVMParserAST::FoldConstantValuesToLiterals(URigVMGraph* InGraph, URigVMController* InController, const TArray<FRigVMExternalVariable>& InExternalVariables, const TArray<FRigVMUserDataArray>& InRigVMUserData)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (InController == nullptr)
	{
		return false;
	}

	if (InRigVMUserData.Num() == 0)
	{
		return false;
	}

	// loop over all call externs and figure out if they are a non-const node
	// with one or more const pins. we then build a temporary VM to run the part of the 
	// graph, and pull out the required values - we then bake the value into a literal 
	// and remove the tree that created the value.

	TMap<FString, FString> ComputedDefaultValues;
	TArray<URigVMPin*> PinsToUpdate;
	TArray<URigVMPin*> RootPinsToUpdate;
	TArray<URigVMPin*> PinsToCompute;
	TArray<URigVMNode*> NodesToCompute;

	const TArray<URigVMNode*> Nodes = InGraph->GetNodes();
	for (URigVMNode* Node : Nodes)
	{
		if (Cast<URigVMParameterNode>(Node) != nullptr ||
			Cast<URigVMVariableNode>(Node) != nullptr ||
			Cast<URigVMEnumNode>(Node) != nullptr)
		{
			continue;
		}

		FRigVMExprAST** NodeExprPtr = SubjectToExpression.Find(Node);
		if(NodeExprPtr == nullptr)
		{
			continue;
		}

		FRigVMExprAST* NodeExpr = *NodeExprPtr;
		if (NodeExpr->IsConstant())
		{
			continue;
		}

		const TArray<URigVMPin*> Pins = Node->GetPins();
		for (URigVMPin* Pin : Pins)
		{
			if (Pin->GetDirection() != ERigVMPinDirection::Input &&
				Pin->GetDirection() != ERigVMPinDirection::IO)
			{
				continue;
			}

			FRigVMExprAST** PinExprPtr = SubjectToExpression.Find(Pin);
			if (PinExprPtr == nullptr)
			{
				continue;
			}
			FRigVMExprAST* PinExpr = *PinExprPtr;
			if (PinExpr->IsA(FRigVMExprAST::EType::Literal))
			{
				if (const FRigVMExprAST* VarPinExpr = PinExpr->GetFirstParentOfType(FRigVMExprAST::EType::Var))
				{
					if (VarPinExpr->GetName() == PinExpr->GetName())
					{
						PinExpr = (FRigVMExprAST*)VarPinExpr;
					}
				}

				// if we are still a literal, carry on
				if (PinExpr->IsA(FRigVMExprAST::EType::Literal))
				{
					continue;
				}
			}

			TArray<URigVMPin*> SourcePins = Pin->GetLinkedSourcePins(true);
			if (SourcePins.Num() == 0)
			{
				continue;
			}

			if (!PinExpr->IsConstant())
			{
				continue;
			}

			bool bFoundValidSourcePin = false;
			for (URigVMPin* SourcePin : SourcePins)
			{
				URigVMNode* SourceNode = SourcePin->GetNode();
				check(SourceNode);

				if (Cast<URigVMParameterNode>(SourceNode) != nullptr ||
					Cast<URigVMVariableNode>(SourceNode) != nullptr ||
					Cast<URigVMRerouteNode>(SourceNode) != nullptr ||
					Cast<URigVMEnumNode>(SourceNode) != nullptr)
				{
					continue;
				}

				PinsToCompute.AddUnique(SourcePin);
				NodesToCompute.AddUnique(SourceNode);
				bFoundValidSourcePin = true;
			}

			if (bFoundValidSourcePin)
			{
				PinsToUpdate.Add(Pin);
				RootPinsToUpdate.AddUnique(Pin->GetRootPin());
			}
		}
	}

	if (NodesToCompute.Num() == 0)
	{
		return false;
	}

	// we now know the node we need to run.
	// let's build an temporary AST which has only those nodes
	TSharedPtr<FRigVMParserAST> TempAST = MakeShareable(new FRigVMParserAST(InGraph, NodesToCompute));

	// build the VM to run this AST
	TMap<FString, FRigVMOperand> Operands;
	URigVM* TempVM = NewObject<URigVM>(GetTransientPackage());
	
	URigVMCompiler* TempCompiler = NewObject<URigVMCompiler>(GetTransientPackage());
	TempCompiler->Settings.ConsolidateWorkRegisters = false;
	TempCompiler->Settings.SetupNodeInstructionIndex = false;

	TempCompiler->Compile(InGraph, InController, TempVM, InExternalVariables, InRigVMUserData, &Operands, TempAST);

	FRigVMMemoryContainer* Memory[] = { TempVM->WorkMemoryPtr, TempVM->LiteralMemoryPtr };

	for (const FRigVMUserDataArray& RigVMUserData : InRigVMUserData)
	{
		TempVM->Execute(FRigVMMemoryContainerPtrArray(Memory, 2), RigVMUserData);
	}

	// copy the values out of the temp VM and set them on the cached value
	for (URigVMPin* PinToCompute : PinsToCompute)
	{
		TGuardValue<bool> GuardControllerNotifs(InController->bSuspendNotifications, true);

		URigVMPin* RootPin = PinToCompute->GetRootPin();

		FRigVMVarExprAST* RootVarExpr = nullptr;
		FRigVMExprAST** RootPinExprPtr = SubjectToExpression.Find(RootPin);
		if (RootPinExprPtr != nullptr)
		{
			FRigVMExprAST* RootPinExpr = *RootPinExprPtr;
			if (RootPinExpr->IsA(FRigVMExprAST::EType::Var))
			{
				RootVarExpr = RootPinExpr->To<FRigVMVarExprAST>();
			}
		}

		FString PinHash = URigVMCompiler::GetPinHash(RootPin, RootVarExpr, false);
		const FRigVMOperand& Operand = Operands.FindChecked(PinHash);
		TArray<FString> DefaultValues = TempVM->GetWorkMemory().GetRegisterValueAsString(Operand, RootPin->GetCPPType(), RootPin->GetCPPTypeObject());
		if (DefaultValues.Num() == 0)
		{
			continue;
		}

		TArray<FString> SegmentNames;
		if (!URigVMPin::SplitPinPath(PinToCompute->GetSegmentPath(), SegmentNames))
		{
			SegmentNames.Add(PinToCompute->GetName());
		}

		FString DefaultValue = DefaultValues[0];
		if (RootPin->IsArray())
		{
			DefaultValue = FString::Printf(TEXT("(%s)"), *FString::Join(DefaultValues, TEXT(",")));
		}

		URigVMPin* PinForDefaultValue = RootPin;
		while (PinForDefaultValue != PinToCompute && SegmentNames.Num() > 0)
		{
			TArray<FString> SplitDefaultValues = URigVMController::SplitDefaultValue(DefaultValue);

			if (PinForDefaultValue->IsArray())
			{
				int32 ElementIndex = FCString::Atoi(*SegmentNames[0]);
				DefaultValue = SplitDefaultValues[ElementIndex];
				PinForDefaultValue = PinForDefaultValue->GetSubPins()[ElementIndex];
				URigVMController::PostProcessDefaultValue(PinForDefaultValue, DefaultValue);
				SegmentNames.RemoveAt(0);
			}
			else if (PinForDefaultValue->IsStruct())
			{
				for (const FString& MemberNameValuePair : SplitDefaultValues)
				{
					FString MemberName, MemberValue;
					if (MemberNameValuePair.Split(TEXT("="), &MemberName, &MemberValue))
					{
						if (MemberName == SegmentNames[0])
						{
							URigVMPin* SubPin = PinForDefaultValue->FindSubPin(MemberName);
							if (SubPin == nullptr)
							{
								SegmentNames.Reset();
								break;
							}

							DefaultValue = MemberValue;
							PinForDefaultValue = SubPin;
							URigVMController::PostProcessDefaultValue(PinForDefaultValue, DefaultValue);
							SegmentNames.RemoveAt(0);
							break;
						}
					}
				}
			}
			else
			{
				checkNoEntry();
			}
		}

		TArray<URigVMPin*> TargetPins = PinToCompute->GetLinkedTargetPins();
		for (URigVMPin* TargetPin : TargetPins)
		{
			PinDefaultValueOverrides.FindOrAdd(TargetPin, DefaultValue);
		}
	}

	// now remove all of the expressions no longer needed
	TArray<FRigVMExprAST*> ExpressionsToRemove;
	for (URigVMPin* RootPinToUpdate : RootPinsToUpdate)
	{
		FRigVMExprAST** PreviousExprPtr = SubjectToExpression.Find(RootPinToUpdate);
		if (PreviousExprPtr)
		{
			FRigVMVarExprAST* PreviousVarExpr = (*PreviousExprPtr)->To<FRigVMVarExprAST>();

			// if the previous var expression is a literal used to initialize a var
			// (for example on an IO pin, or when we are driving sub pins)
			if (PreviousVarExpr->IsA(FRigVMExprAST::EType::Literal))
			{
				bool bRedirectedVar = false;
				for (int32 ParentIndex = 0; ParentIndex < PreviousVarExpr->NumParents(); ParentIndex++)
				{
					const FRigVMExprAST* ParentExpr = PreviousVarExpr->ParentAt(ParentIndex);
					if (ParentExpr->IsA(FRigVMExprAST::EType::Assign))
					{
						for (int32 GrandParentIndex = 0; GrandParentIndex < ParentExpr->NumParents(); GrandParentIndex++)
						{
							const FRigVMExprAST* GrandParentExpr = ParentExpr->ParentAt(GrandParentIndex);
							if (GrandParentExpr->IsA(FRigVMExprAST::EType::Block))
							{
								GrandParentExpr = GrandParentExpr->GetParent();
							}
							if (GrandParentExpr->IsA(FRigVMExprAST::EType::Var) && (GrandParentExpr->GetName() == PreviousVarExpr->GetName()))
							{
								PreviousVarExpr = (FRigVMVarExprAST*)GrandParentExpr->To<FRigVMVarExprAST>();
								bRedirectedVar = true;
								break;
							}
						}
					}

					if (bRedirectedVar)
					{
						break;
					}
				}
			}

			FRigVMLiteralExprAST* LiteralExpr = MakeExpr<FRigVMLiteralExprAST>(RootPinToUpdate);
			LiteralExpr->Name = PreviousVarExpr->Name;
			SubjectToExpression[RootPinToUpdate] = LiteralExpr;
			PreviousVarExpr->ReplaceBy(LiteralExpr);
			ExpressionsToRemove.Add(PreviousVarExpr);
		}
	}

	RemoveExpressions(ExpressionsToRemove);

	return ExpressionsToRemove.Num() > 0;
}

bool FRigVMParserAST::FoldUnreachableBranches(URigVMGraph* InGraph)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	TArray<FRigVMExprAST*> ExpressionsToRemove;

	const TArray<URigVMNode*> Nodes = InGraph->GetNodes();
	for (URigVMNode* Node : Nodes)
	{
		if (Cast<URigVMParameterNode>(Node) != nullptr ||
			Cast<URigVMVariableNode>(Node) != nullptr)
		{
			continue;
		}

		FRigVMExprAST** NodeExprPtr = SubjectToExpression.Find(Node);
		if (NodeExprPtr == nullptr)
		{
			continue;
		}

		FRigVMExprAST* NodeExpr = *NodeExprPtr;
		if (NodeExpr->NumParents() == 0)
		{
			continue;
		}

		if (NodeExpr->IsA(FRigVMExprAST::EType::Branch))
		{
			const FRigVMBranchExprAST* BranchExpr = NodeExpr->To<FRigVMBranchExprAST>();
			FRigVMExprAST* ExprReplacement = nullptr;

			if (BranchExpr->IsAlwaysTrue())
			{
				ExprReplacement = (FRigVMExprAST*)BranchExpr->GetTrueExpr();
			}
			else if (BranchExpr->IsAlwaysFalse())
			{
				ExprReplacement = (FRigVMExprAST*)BranchExpr->GetFalseExpr();
			}

			if (ExprReplacement)
			{
				if (ExprReplacement->NumChildren() == 1)
				{
					ExprReplacement = (FRigVMExprAST*)ExprReplacement->ChildAt(0);
					if (ExprReplacement->IsA(FRigVMExprAST::EType::Block))
					{
						ExprReplacement->RemoveParent((FRigVMExprAST*)ExprReplacement->GetParent());
						NodeExpr->ReplaceBy(ExprReplacement);
						ExpressionsToRemove.Add(NodeExpr);
					}
				}
			}
		}
		else
		{
			FRigVMExprAST* CachedValueExpr = (FRigVMExprAST*)NodeExpr->GetParent();
			if (!CachedValueExpr->IsA(FRigVMExprAST::EType::CachedValue))
			{
				continue;
			}

			FRigVMExprAST* ExprReplacement = nullptr;
			if (NodeExpr->IsA(FRigVMExprAST::EType::If))
			{
				const FRigVMIfExprAST* IfExpr = NodeExpr->To<FRigVMIfExprAST>();
				if (IfExpr->IsAlwaysTrue())
				{
					ExprReplacement = (FRigVMExprAST*)IfExpr->GetTrueExpr();
				}
				else if (IfExpr->IsAlwaysFalse())
				{
					ExprReplacement = (FRigVMExprAST*)IfExpr->GetFalseExpr();
				}
			}
			else if (NodeExpr->IsA(FRigVMExprAST::EType::Select))
			{
				const FRigVMSelectExprAST* SelectExpr = NodeExpr->To<FRigVMSelectExprAST>();
				int32 ConstantCaseIndex = SelectExpr->GetConstantValueIndex();
				if (ConstantCaseIndex != INDEX_NONE)
				{
					ExprReplacement = (FRigVMExprAST*)SelectExpr->GetValueExpr(ConstantCaseIndex);
				}
			}

			if (ExprReplacement)
			{
				ExprReplacement->RemoveParent((FRigVMExprAST*)ExprReplacement->GetParent());
				CachedValueExpr->ReplaceBy(ExprReplacement);
				ExpressionsToRemove.Add(CachedValueExpr);
			}
		}
	}

	RemoveExpressions(ExpressionsToRemove);
	return ExpressionsToRemove.Num() > 0;
}

void FRigVMParserAST::FoldLiterals()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	TMap<FString, FRigVMLiteralExprAST*> ValueToLiteral;
	TArray<FRigVMExprAST*> ExpressionsToRemove;

	for (int32 ExpressionIndex = 0; ExpressionIndex < Expressions.Num(); ExpressionIndex++)
	{
		FRigVMExprAST* Expression = Expressions[ExpressionIndex];
		if (Expression->Parents.Num() == 0)
		{
			continue;
		}

		if (Expression->GetType() == FRigVMExprAST::EType::Literal)
		{
			ensure(Expression->Children.Num() == 0);

			FRigVMLiteralExprAST* LiteralExpr = Expression->To<FRigVMLiteralExprAST>();
			FString DefaultValue = LiteralExpr->GetDefaultValue();
			if (DefaultValue.IsEmpty())
			{
				continue;
			}

			FString Hash = FString::Printf(TEXT("[%s] %s"), *LiteralExpr->GetCPPType(), *DefaultValue);

			FRigVMLiteralExprAST* const* MappedExpr = ValueToLiteral.Find(Hash);
			if (MappedExpr)
			{
				TArray<FRigVMExprAST*> Parents = Expression->Parents;
				for (FRigVMExprAST* Parent : Parents)
				{
					Parent->ReplaceChild(Expression, *MappedExpr);
				}
				ExpressionsToRemove.AddUnique(Expression);
			}
			else
			{
				ValueToLiteral.Add(Hash, LiteralExpr);
			}
		}
	}

	RemoveExpressions(ExpressionsToRemove);
}

const FRigVMExprAST* FRigVMParserAST::GetExprForSubject(UObject* InSubject)
{
	if (FRigVMExprAST* const* ExpressionPtr = SubjectToExpression.Find(InSubject))
	{
		return *ExpressionPtr;
	}
	return nullptr;
}

void FRigVMParserAST::PrepareCycleChecking(URigVMPin* InPin)
{
	if (InPin == nullptr)
	{
		LastCycleCheckExpr = nullptr;
		CycleCheckFlags.Reset();
		return;
	}

	const FRigVMExprAST* Expression = nullptr;
	if (FRigVMExprAST* const* ExpressionPtr = SubjectToExpression.Find(InPin->GetNode()))
	{
		Expression = *ExpressionPtr;
	}
	else
	{
		return;
	}

	if (LastCycleCheckExpr != Expression)
	{
		LastCycleCheckExpr = Expression;
		CycleCheckFlags.SetNumZeroed(Expressions.Num());
		CycleCheckFlags[LastCycleCheckExpr->GetIndex()] = ETraverseRelationShip_Self;
	}
}

bool FRigVMParserAST::CanLink(URigVMPin* InSourcePin, URigVMPin* InTargetPin, FString* OutFailureReason)
{
	if (InSourcePin == nullptr || InTargetPin == nullptr || InSourcePin == InTargetPin)
	{
		if (OutFailureReason)
		{
			*OutFailureReason = FString(TEXT("Provided objects contain nullptr."));
		}
		return false;
	}

	URigVMNode* SourceNode = InSourcePin->GetNode();
	URigVMNode* TargetNode = InTargetPin->GetNode();
	if (SourceNode == TargetNode)
	{
		if (OutFailureReason)
		{
			*OutFailureReason = FString(TEXT("Source and Target Nodes are identical."));
		}
		return false;
	}

	const FRigVMExprAST* SourceExpression = nullptr;
	if (FRigVMExprAST* const* SourceExpressionPtr = SubjectToExpression.Find(SourceNode))
	{
		SourceExpression = *SourceExpressionPtr;
	}
	else
	{
		if (OutFailureReason)
		{
			*OutFailureReason = FString(TEXT("Source node is not part of AST."));
		}
		return false;
	}

	const FRigVMVarExprAST* SourceVarExpression = nullptr;
	if (FRigVMExprAST* const* SourceVarExpressionPtr = SubjectToExpression.Find(InSourcePin->GetRootPin()))
	{
		if ((*SourceVarExpressionPtr)->IsA(FRigVMExprAST::EType::Var))
		{
			SourceVarExpression = (*SourceVarExpressionPtr)->To<FRigVMVarExprAST>();
		}
	}

	const FRigVMExprAST* TargetExpression = nullptr;
	if (FRigVMExprAST* const* TargetExpressionPtr = SubjectToExpression.Find(TargetNode))
	{
		TargetExpression = *TargetExpressionPtr;
	}
	else
	{
		if (OutFailureReason)
		{
			*OutFailureReason = FString(TEXT("Target node is not part of AST."));
		}
		return false;
	}

	const FRigVMBlockExprAST* SourceBlock = SourceExpression->GetBlock();
	const FRigVMBlockExprAST* TargetBlock = TargetExpression->GetBlock();
	if (SourceBlock == nullptr || TargetBlock == nullptr)
	{
		return false;
	}

	if (SourceBlock == TargetBlock ||
		SourceBlock->Contains(TargetBlock) ||
		TargetBlock->Contains(SourceBlock) ||
		TargetBlock->GetRootBlock()->Contains(SourceBlock) ||
		SourceBlock->GetRootBlock()->Contains(TargetBlock))
	{
		if (SourceVarExpression)
		{
			if (SourceVarExpression->SupportsSoftLinks())
			{
				return true;
			}
		}

		if (LastCycleCheckExpr != SourceExpression && LastCycleCheckExpr != TargetExpression)
		{
			PrepareCycleChecking(InSourcePin);
		}

		TArray<ETraverseRelationShip>& Flags = CycleCheckFlags;
		TraverseParents(LastCycleCheckExpr, [&Flags](const FRigVMExprAST* InExpr) -> bool {
			if (Flags[InExpr->GetIndex()] == ETraverseRelationShip_Self)
			{
				return true;
			}
			if (Flags[InExpr->GetIndex()] != ETraverseRelationShip_Unknown)
			{
				return false;
			}
			if (InExpr->IsA(FRigVMExprAST::EType::Var))
			{
				if (InExpr->To<FRigVMVarExprAST>()->SupportsSoftLinks())
				{
					return false;
				}
			}
			Flags[InExpr->GetIndex()] = ETraverseRelationShip_Parent;
			return true;
		});

		TraverseChildren(LastCycleCheckExpr, [&Flags](const FRigVMExprAST* InExpr) -> bool {
			if (Flags[InExpr->GetIndex()] == ETraverseRelationShip_Self)
			{
				return true;
			}
			if (Flags[InExpr->GetIndex()] != ETraverseRelationShip_Unknown)
			{
				return false;
			}
			if (InExpr->IsA(FRigVMExprAST::EType::Var))
			{
				if (InExpr->To<FRigVMVarExprAST>()->SupportsSoftLinks())
				{
					return false;
				}
			}
			Flags[InExpr->GetIndex()] = ETraverseRelationShip_Child;
			return true;
		});

		bool bFoundCycle = false;
		if (LastCycleCheckExpr == SourceExpression)
		{
			bFoundCycle = Flags[TargetExpression->GetIndex()] == ETraverseRelationShip_Child;
		}
		else
		{
			bFoundCycle = Flags[SourceExpression->GetIndex()] == ETraverseRelationShip_Parent;
		}

		if (bFoundCycle)
		{
			if (OutFailureReason)
			{
				*OutFailureReason = FString(TEXT("Cycles are not allowed."));
			}
			return false;
		}
	}
	else
	{
		// if one of the blocks is not part of the current 
		// execution - that's fine.
		if (SourceBlock->GetRootBlock()->ContainsEntry() != 
			TargetBlock->GetRootBlock()->ContainsEntry())
		{
			return true;
		}

		if (OutFailureReason)
		{
			*OutFailureReason = FString::Printf(TEXT("You cannot combine nodes from \"%s\" and \"%s\"."), *SourceBlock->GetName().ToString(), *TargetBlock->GetName().ToString());
		}
		return false;
	}

	return true;
}

FString FRigVMParserAST::DumpText() const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	FString Result;
	for (FRigVMExprAST* RootExpr : RootExpressions)
	{
		Result += TEXT("\n") + RootExpr->DumpText();
	}
	return Result;
}

FString FRigVMParserAST::DumpDot() const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	TArray<bool> OutExpressionDefined;
	OutExpressionDefined.AddZeroed(Expressions.Num());

	FString Result = TEXT("digraph AST {\n  node [style=filled];\n  rankdir=\"LR\";");

	for (FRigVMExprAST* RootExpr : RootExpressions)
	{
		Result += RootExpr->DumpDot(OutExpressionDefined, TEXT("  "));
	}
	Result += TEXT("\n}");
	return Result;
}

FRigVMBlockExprAST* FRigVMParserAST::GetObsoleteBlock()
{
	if (ObsoleteBlock == nullptr)
	{
		ObsoleteBlock = MakeExpr<FRigVMBlockExprAST>(FRigVMExprAST::EType::Block);
		ObsoleteBlock->bIsObsolete = true;
		RootExpressions.Add(ObsoleteBlock);
	}
	return ObsoleteBlock;
}

const FRigVMBlockExprAST* FRigVMParserAST::GetObsoleteBlock() const
{
	if (ObsoleteBlock == nullptr)
	{
		FRigVMParserAST* MutableThis = (FRigVMParserAST*)this;
		MutableThis->ObsoleteBlock = MutableThis->MakeExpr<FRigVMBlockExprAST>(FRigVMExprAST::EType::Block);
		MutableThis->ObsoleteBlock->bIsObsolete = true;
		MutableThis->RootExpressions.Add(MutableThis->ObsoleteBlock);
	}
	return ObsoleteBlock;
}

void FRigVMParserAST::RemoveExpression(FRigVMExprAST* InExpr, bool bRefreshIndices, bool bRecurseToChildren)
{
	TArray<FRigVMExprAST*> Parents = InExpr->Parents;
	for (FRigVMExprAST* Parent : Parents)
	{
		InExpr->RemoveParent(Parent);
	}
	TArray<FRigVMExprAST*> Children = InExpr->Children;
	for (FRigVMExprAST* Child : Children)
	{
		Child->RemoveParent(InExpr);

		if (bRecurseToChildren && Child->NumParents() == 0)
		{
			RemoveExpression(Child, false, bRecurseToChildren);
		}
	}

	Expressions.Remove(InExpr);

	TArray<UObject*> KeysToRemove;
	for (TPair<UObject*, FRigVMExprAST*> Pair : SubjectToExpression)
	{
		if (Pair.Value == InExpr)
		{
			KeysToRemove.Add(Pair.Key);
		}
	}
	for (UObject* KeyToRemove : KeysToRemove)
	{
		SubjectToExpression.Remove(KeyToRemove);
	}

	delete InExpr;

	if (bRefreshIndices)
	{
		RefreshExprIndices();
	}
}

void FRigVMParserAST::RemoveExpressions(TArray<FRigVMExprAST*> InExprs, bool bRefreshIndices, bool bRecurseToChildren)
{
	for (FRigVMExprAST* InExpr : InExprs)
	{
		RemoveExpression(InExpr, false, bRecurseToChildren);
	}
	if (bRefreshIndices)
	{
		RefreshExprIndices();
	}
}

void FRigVMParserAST::TraverseParents(const FRigVMExprAST* InExpr, TFunctionRef<bool(const FRigVMExprAST*)> InContinuePredicate)
{
	if (!InContinuePredicate(InExpr))
	{
		return;
	}
	for (const FRigVMExprAST* ParentExpr : InExpr->Parents)
	{
		TraverseParents(ParentExpr, InContinuePredicate);
	}
}

void FRigVMParserAST::TraverseChildren(const FRigVMExprAST* InExpr, TFunctionRef<bool(const FRigVMExprAST*)> InContinuePredicate)
{
	if (!InContinuePredicate(InExpr))
	{
		return;
	}
	for (const FRigVMExprAST* ChildExpr : InExpr->Children)
	{
		TraverseChildren(ChildExpr, InContinuePredicate);
	}
}

