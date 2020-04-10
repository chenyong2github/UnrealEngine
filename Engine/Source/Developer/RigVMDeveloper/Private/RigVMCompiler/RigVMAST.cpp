// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCompiler/RigVMAST.h"
#include "RigVMModel/Nodes/RigVMStructNode.h"
#include "RigVMModel/Nodes/RigVMParameterNode.h"
#include "RigVMModel/Nodes/RigVMVariableNode.h"
#include "RigVMModel/Nodes/RigVMCommentNode.h"
#include "RigVMModel/Nodes/RigVMRerouteNode.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMCore/RigVMExecuteContext.h"
#include "Stats/StatsHierarchical.h"

FRigVMExprAST::FRigVMExprAST(const FRigVMParserAST* InParser, EType InType)
	: Name(NAME_None)
	, Type(InType)
	, Index(INDEX_NONE)
	, ParserPtr(InParser)
{
	if (InParser)
	{
		Index = InParser->Expressions.Num();
		((FRigVMParserAST*)ParserPtr)->Expressions.Add(this);
	}
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

const FRigVMBlockExprAST* FRigVMExprAST::GetBlock() const
{
	if (Parents.Num() == 0)
	{
		if (IsA(EType::Block))
		{
			return To<FRigVMBlockExprAST>();
		}
		return nullptr;
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
			{
				Result += FString::Printf(TEXT("\n%snode_%d [shape = Mdiamond];"), *Prefix, GetIndex());
				break;
			}
			case EType::Assign:
			case EType::Copy:
			case EType::CallExtern:
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
	for (FRigVMExprAST* Expression : *this)
	{
		if (Expression == InExpression)
		{
			return true;
		}

		if (Expression->IsA(EType::Block))
		{
			if (Expression->To<FRigVMBlockExprAST>()->Contains(InExpression))
			{
				return true;
			}
		}
	}
	return false;
}

FName FRigVMEntryExprAST::GetEventName() const
{
	if (URigVMNode* EventNode = GetNode())
	{
		return EventNode->GetEventName();
	}
	return NAME_None;
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
	return Pin->GetDefaultValue();
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
		return Pin->GetName() == TEXT("Value");
	}
	return false;
}

FRigVMParserAST::FRigVMParserAST(URigVMGraph* InGraph, const FRigVMParserASTSettings& InSettings)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()
	
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
	FRigVMBlockExprAST* ObsoleteBlock = nullptr;
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
				if (ObsoleteBlock == nullptr)
				{
					ObsoleteBlock = new FRigVMBlockExprAST(this);
					RootExpressions.Add(ObsoleteBlock);
				}
				if (bTraverseMutable)
				{
					TraverseMutableNode(Nodes[NodeIndex], ObsoleteBlock);
				}
				else
				{
					TraverseNode(Nodes[NodeIndex], ObsoleteBlock);
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

	if (InSettings.bFoldAssignments)
	{
		FoldAssignments();
	}

	if (InSettings.bFoldLiterals)
	{
		FoldLiterals();
	}
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

	FRigVMExprAST* NodeExpr = nullptr;
	if (InNode->IsEvent())
	{
		NodeExpr = new FRigVMEntryExprAST(this, InNode);
		NodeExpr->Name = InNode->GetEventName();
	}
	else
	{
		if (Cast<URigVMRerouteNode>(InNode) ||
			Cast<URigVMParameterNode>(InNode) ||
			Cast<URigVMVariableNode>(InNode))
		{
			NodeExpr = new FRigVMNoOpExprAST(this, InNode);
		}
		else
		{
			NodeExpr = new FRigVMCallExternExprAST(this, InNode);
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
		InParentExpr = NodeExpr;
	}
	SubjectToExpression.Add(InNode, NodeExpr);
	NodeExpressionIndex.Add(InNode, NodeExpr->GetIndex());

	TraversePins(InNode, NodeExpr);

	for (URigVMPin* SourcePin : InNode->GetPins())
	{
		if (SourcePin->GetDirection() == ERigVMPinDirection::Output || SourcePin->GetDirection() == ERigVMPinDirection::IO)
		{
			if (SourcePin->GetScriptStruct()->IsChildOf(FRigVMExecuteContext::StaticStruct()))
			{
				TArray<URigVMPin*> TargetPins = SourcePin->GetLinkedTargetPins();
				for (URigVMPin* TargetPin : TargetPins)
				{
					TraverseMutableNode(TargetPin->GetNode(), InParentExpr);
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

	FRigVMExprAST* NodeExpr = nullptr;
	if (Cast<URigVMRerouteNode>(InNode) ||
		Cast<URigVMParameterNode>(InNode) ||
		Cast<URigVMVariableNode>(InNode))
	{
		NodeExpr = new FRigVMNoOpExprAST(this, InNode);
	}
	else
	{
		NodeExpr = new FRigVMCallExternExprAST(this, InNode);
	}
	NodeExpr->Name = InNode->GetFName();
	NodeExpr->AddParent(InParentExpr);
	SubjectToExpression.Add(InNode, NodeExpr);
	NodeExpressionIndex.Add(InNode, NodeExpr->GetIndex());

	TraversePins(InNode, NodeExpr);

	return NodeExpr;
}

TArray<FRigVMExprAST*> FRigVMParserAST::TraversePins(URigVMNode* InNode, FRigVMExprAST* InParentExpr)
{
	TArray<FRigVMExprAST*> PinExpressions;

	for (URigVMPin* Pin : InNode->GetPins())
	{
		PinExpressions.Add(TraversePin(Pin, InParentExpr));
	}

	return PinExpressions;
}

FRigVMExprAST* FRigVMParserAST::TraversePin(URigVMPin* InPin, FRigVMExprAST* InParentExpr)
{
	ensure(!SubjectToExpression.Contains(InPin));

	TArray<URigVMLink*> SourceLinks = InPin->GetSourceLinks(true);

	FRigVMExprAST* PinExpr = nullptr;

	if (Cast<URigVMParameterNode>(InPin->GetNode()) ||
		Cast<URigVMVariableNode>(InPin->GetNode()))
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
		PinExpr = new FRigVMLiteralExprAST(this, InPin);
	}
	else
	{
		PinExpr = new FRigVMVarExprAST(this, InPin);
	}
	PinExpr->AddParent(InParentExpr);
	PinExpr->Name = *InPin->GetPinPath();
	SubjectToExpression.Add(InPin, PinExpr);

	if (InPin->GetScriptStruct()->IsChildOf(FRigVMExecuteContext::StaticStruct()))
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

		if (!bHasSourceLinkToRoot && (InPin->GetDirection() == ERigVMPinDirection::IO || SourceLinks.Num() > 0))
		{
			FRigVMLiteralExprAST* LiteralExpr = new FRigVMLiteralExprAST(this, InPin);
			FRigVMCopyExprAST* LiteralCopyExpr = new FRigVMCopyExprAST(this, InPin, InPin);
			LiteralCopyExpr->Name = *FString::Printf(TEXT("%s -> %s"), *InPin->GetPinPath(), *InPin->GetPinPath());
			LiteralCopyExpr->AddParent(PinExpr);
			LiteralExpr->AddParent(LiteralCopyExpr);
			LiteralExpr->Name = *InPin->GetPinPath();

			SubjectToExpression.Remove(InPin);
			SubjectToExpression.Add(InPin, LiteralExpr);
		}
	}


	for (URigVMLink* SourceLink : SourceLinks)
	{
		TraverseLink(SourceLink, PinExpr);
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
		AssignExpr = new FRigVMCopyExprAST(this, SourcePin, TargetPin);
	}
	else
	{
		AssignExpr = new FRigVMAssignExprAST(this, SourcePin, TargetPin);
	}

	AssignExpr->Name = *InLink->GetPinPathRepresentation();
	AssignExpr->AddParent(InParentExpr);
	SubjectToExpression.Add(InLink, AssignExpr);

	FRigVMExprAST* NodeExpr = TraverseNode(SourcePin->GetNode(), AssignExpr);

	// if this is a copy expression - we should require the copy to use a ref instead
	if (NodeExpr->IsA(FRigVMExprAST::EType::CallExtern))
	{
		for (FRigVMExprAST* ChildExpr : *NodeExpr)
		{
			if (ChildExpr->IsA(FRigVMExprAST::EType::Var))
			{
				FRigVMVarExprAST* VarExpr = ChildExpr->To<FRigVMVarExprAST>();
				if (VarExpr->GetPin() == SourceRootPin)
				{
					FRigVMCachedValueExprAST* CacheExpr = nullptr;
					for (FRigVMExprAST* VarExprParent : VarExpr->Parents)
					{
						if (VarExprParent->IsA(FRigVMExprAST::EType::CachedValue))
						{
							CacheExpr = VarExprParent->To<FRigVMCachedValueExprAST>();
							break;
						}
					}

					if(CacheExpr == nullptr)
					{
						CacheExpr = new FRigVMCachedValueExprAST(this);
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
				FRigVMNoOpExprAST* NoOpExpr = new FRigVMNoOpExprAST(this, Entry->GetNode());
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
				FRigVMExprAST* ExitExpr = new FRigVMExitExprAST(this);
				ExitExpr->AddParent(RootExpr);
				Expressions.AddUnique(ExitExpr);
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

		FRigVMExprAST* Parent = AssignExpr->Parents[0];
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
			FString Hash = FString::Printf(TEXT("[%s] %s"), *LiteralExpr->GetCPPType(), *LiteralExpr->GetDefaultValue());

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
		TargetBlock->Contains(SourceBlock))
	{
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
		if (SourceBlock->GetRootBlock()->ContainsEntry() != 
			TargetBlock->GetRootBlock()->ContainsEntry())
		{
			return true;
		}

		if (OutFailureReason)
		{
			*OutFailureReason = FString(TEXT("Blocks are not compatible."));
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

	FString Result = TEXT("digraph AST {\n  node [style=filled];\n  rankdir=\"RL\";");

	for (FRigVMExprAST* RootExpr : RootExpressions)
	{
		Result += RootExpr->DumpDot(OutExpressionDefined, TEXT("  "));
	}
	Result += TEXT("\n}");
	return Result;
}

void FRigVMParserAST::RemoveExpression(FRigVMExprAST* InExpr, bool bRefreshIndices)
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

void FRigVMParserAST::RemoveExpressions(TArray<FRigVMExprAST*> InExprs, bool bRefreshIndices)
{
	for (FRigVMExprAST* InExpr : InExprs)
	{
		RemoveExpression(InExpr, false);
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

