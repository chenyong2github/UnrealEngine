// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMFunctionLibrary.h"
#include "RigVMModel/RigVMController.h"
#include "RigVMModel/Nodes/RigVMLibraryNode.h"
#include "RigVMModel/Nodes/RigVMFunctionEntryNode.h"
#include "RigVMModel/Nodes/RigVMFunctionReturnNode.h"
#include "UObject/Package.h"

URigVMGraph::URigVMGraph()
: DiagnosticsAST(nullptr)
, RuntimeAST(nullptr)
{
}

const TArray<URigVMNode*>& URigVMGraph::GetNodes() const
{
	return Nodes;
}

const TArray<URigVMLink*>& URigVMGraph::GetLinks() const
{
	return Links;
}

TArray<URigVMGraph*> URigVMGraph::GetContainedGraphs(bool bRecursive) const
{
	TArray<URigVMGraph*> Graphs;
	for (URigVMNode* Node : GetNodes())
	{
		if (URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(Node))
		{
			Graphs.Add(CollapseNode->GetContainedGraph());
			if (bRecursive)
			{
				Graphs.Append(CollapseNode->GetContainedGraph()->GetContainedGraphs(true));
			}
		}
	}
	return Graphs;
}

URigVMGraph* URigVMGraph::GetParentGraph() const
{
	if(URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(GetOuter()))
	{
		return CollapseNode->GetGraph();
	}
	return nullptr;
}

URigVMGraph* URigVMGraph::GetRootGraph() const
{
	if(URigVMGraph* ParentGraph = GetParentGraph())
	{
		return ParentGraph->GetRootGraph();
	}
	return (URigVMGraph*)this;
}

bool URigVMGraph::IsRootGraph() const
{
	return GetRootGraph() == this;
}

URigVMFunctionEntryNode* URigVMGraph::GetEntryNode() const
{
	for (URigVMNode* Node : Nodes)
	{
		if (URigVMFunctionEntryNode* EntryNode = Cast<URigVMFunctionEntryNode>(Node))
		{
			return EntryNode;
		}
	}
	return nullptr;
}

URigVMFunctionReturnNode* URigVMGraph::GetReturnNode() const
{
	for (URigVMNode* Node : Nodes)
	{
		if (URigVMFunctionReturnNode* ReturnNode = Cast<URigVMFunctionReturnNode>(Node))
		{
			return ReturnNode;
		}
	}
	return nullptr;
}

TArray<FRigVMGraphVariableDescription> URigVMGraph::GetVariableDescriptions() const
{
	TArray<FRigVMGraphVariableDescription> Variables;
	for (URigVMNode* Node : Nodes)
	{
		if (URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
		{
			Variables.AddUnique(VariableNode->GetVariableDescription());
		}
	}
	return Variables;
}

TArray<FRigVMGraphParameterDescription> URigVMGraph::GetParameterDescriptions() const
{
	TArray<FRigVMGraphParameterDescription> Parameters;
	for (URigVMNode* Node : Nodes)
	{
		if (URigVMParameterNode* ParameterNode = Cast<URigVMParameterNode>(Node))
		{
			Parameters.AddUnique(ParameterNode->GetParameterDescription());
		}
	}
	return Parameters;
}

FString URigVMGraph::GetNodePath() const
{
	if (URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(GetOuter()))
	{
		return CollapseNode->GetNodePath(true /* recursive */);
	}
	return FString();
}

FString URigVMGraph::GetGraphName() const
{
	if (URigVMCollapseNode* CollapseNode = Cast<URigVMCollapseNode>(GetOuter()))
	{
		return CollapseNode->GetNodePath(false /* recursive */);
	}
	return FString();
}

URigVMNode* URigVMGraph::FindNodeByName(const FName& InNodeName) const
{
	for (URigVMNode* Node : Nodes)
	{
		if (Node == nullptr)
		{
			continue;
		}

		if (Node->GetFName() == InNodeName)
		{
			return Node;
		}
	}
	return nullptr;
}

URigVMNode* URigVMGraph::FindNode(const FString& InNodePath) const
{
	if (InNodePath.IsEmpty())
	{
		return nullptr;
	}

	FString Left = InNodePath, Right;
	URigVMNode::SplitNodePathAtStart(InNodePath, Left, Right);

	if (Right.IsEmpty())
	{
		return FindNodeByName(*Left);
	}

	if (URigVMLibraryNode* LibraryNode = Cast< URigVMLibraryNode>(FindNodeByName(*Left)))
	{
		return LibraryNode->GetContainedGraph()->FindNode(Right);
	}

	return nullptr;
}

URigVMPin* URigVMGraph::FindPin(const FString& InPinPath) const
{
	FString Left, Right;
	if (!URigVMPin::SplitPinPathAtStart(InPinPath, Left, Right))
	{
		Left = InPinPath;
	}

	URigVMNode* Node = FindNode(Left);
	if (Node)
	{
		return Node->FindPin(Right);
	}

	return nullptr;
}

URigVMLink* URigVMGraph::FindLink(const FString& InLinkPinPathRepresentation) const
{
	for(URigVMLink* Link : Links)
	{
		if(Link->GetPinPathRepresentation() == InLinkPinPathRepresentation)
		{
			return Link;
		}
	}
	return nullptr;
}

bool URigVMGraph::IsNodeSelected(const FName& InNodeName) const
{
	return SelectedNodes.Contains(InNodeName);
}

const TArray<FName>& URigVMGraph::GetSelectNodes() const
{
	return SelectedNodes;
}

bool URigVMGraph::IsTopLevelGraph() const
{
	if (GetOuter()->IsA<URigVMLibraryNode>())
	{
		return false;
	}
	return true;
}

URigVMFunctionLibrary* URigVMGraph::GetDefaultFunctionLibrary() const
{
	if (DefaultFunctionLibraryPtr.IsValid())
	{
		return CastChecked<URigVMFunctionLibrary>(DefaultFunctionLibraryPtr.Get());
	}

	if (URigVMLibraryNode* OuterLibraryNode = Cast<URigVMLibraryNode>(GetOuter()))
	{
		if (URigVMGraph* OuterGraph = OuterLibraryNode->GetGraph())
		{
			return OuterGraph->GetDefaultFunctionLibrary();
		}
	}
	return nullptr;
}

void URigVMGraph::SetDefaultFunctionLibrary(URigVMFunctionLibrary* InFunctionLibrary)
{
	DefaultFunctionLibraryPtr = InFunctionLibrary;
}

bool URigVMGraph::AddLocalVariable(const FRigVMGraphVariableDescription& NewVar)
{
	for (FRigVMGraphVariableDescription Variable : LocalVariables)
	{
		if (Variable.Name == NewVar.Name)
		{
			return false;
		}
	}
	
	LocalVariables.Add(NewVar);
	return true;
}

bool URigVMGraph::RemoveLocalVariable(const FName& InVariableName)
{
	int32 FoundIndex = INDEX_NONE;
	for (int32 Index = 0; Index < LocalVariables.Num(); ++Index)
	{
		if (LocalVariables[Index].Name == InVariableName)
		{
			FoundIndex = Index;
			break;
		}
	}

	if (FoundIndex != INDEX_NONE)
	{
		LocalVariables.RemoveAt(FoundIndex);
		return true;
	}
	return false;
}

TArray<FRigVMExternalVariable> URigVMGraph::GetExternalVariables() const
{
	TArray<FRigVMExternalVariable> Variables;
	
	for(URigVMNode* Node : GetNodes())
	{
		if(URigVMLibraryNode* LibraryNode = Cast<URigVMLibraryNode>(Node))
		{
			TArray<FRigVMExternalVariable> LibraryVariables = LibraryNode->GetExternalVariables();
			for(const FRigVMExternalVariable& LibraryVariable : LibraryVariables)
			{
				FRigVMExternalVariable::MergeExternalVariable(Variables, LibraryVariable);
			}
		}
		else if(URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
		{
			// Make sure it is not a local variable
			bool bFoundLocalVariable = false;
			for (FRigVMGraphVariableDescription& LocalVariable : Node->GetGraph()->LocalVariables)
			{
				if (LocalVariable.Name == VariableNode->GetVariableName())
				{
					bFoundLocalVariable = true;
					break;
				}
			}

			if (!bFoundLocalVariable)
			{
				FRigVMExternalVariable::MergeExternalVariable(Variables, VariableNode->GetVariableDescription().ToExternalVariable());
			}
		}
	}
	
	return Variables;
}

FRigVMGraphModifiedEvent& URigVMGraph::OnModified()
{
	return ModifiedEvent;
}

void URigVMGraph::Notify(ERigVMGraphNotifType InNotifType, UObject* InSubject)
{
	ModifiedEvent.Broadcast(InNotifType, this, InSubject);
}

TSharedPtr<FRigVMParserAST> URigVMGraph::GetDiagnosticsAST(bool bForceRefresh, TArray<URigVMLink*> InLinksToSkip)
{
	// only refresh the diagnostics AST if have a different set of links to skip
	if (!bForceRefresh && DiagnosticsAST.IsValid())
	{
		const TArray<URigVMLink*> PreviousLinksToSkip = DiagnosticsAST->GetSettings().LinksToSkip;
		if (PreviousLinksToSkip.Num() < InLinksToSkip.Num())
		{
			bForceRefresh = true;
		}
		else
		{
			for (int32 LinkIndex = 0; LinkIndex < InLinksToSkip.Num(); LinkIndex++)
			{
				if (PreviousLinksToSkip[LinkIndex] != InLinksToSkip[LinkIndex])
				{
					bForceRefresh = true;
					break;
				}
			}
		}
	}

	if (DiagnosticsAST == nullptr || bForceRefresh)
	{
		FRigVMParserASTSettings Settings = FRigVMParserASTSettings::Fast();
		Settings.LinksToSkip = InLinksToSkip;
		DiagnosticsAST = MakeShareable(new FRigVMParserAST(this, nullptr, Settings));
	}
	return DiagnosticsAST;
}

TSharedPtr<FRigVMParserAST> URigVMGraph::GetRuntimeAST(const FRigVMParserASTSettings& InSettings, bool bForceRefresh)
{
	if (RuntimeAST == nullptr || bForceRefresh)
	{
		RuntimeAST = MakeShareable(new FRigVMParserAST(this, nullptr, InSettings));
	}
	return RuntimeAST;
}

void URigVMGraph::ClearAST(bool bClearDiagnostics, bool bClearRuntime)
{
	if (bClearDiagnostics)
	{
		DiagnosticsAST.Reset();
	}
	if (bClearRuntime)
	{
		RuntimeAST.Reset();
	}
}

bool URigVMGraph::IsNameAvailable(const FString& InName)
{
	for (URigVMNode* Node : Nodes)
	{
		if (Node->GetName() == InName)
		{
			return false;
		}
	}
	return true;
}

void URigVMGraph::PrepareCycleChecking(URigVMPin* InPin, bool bAsInput)
{
	TArray<URigVMLink*> LinksToSkip;
	if (InPin)
	{
		LinksToSkip = InPin->GetLinks();
	}

	GetDiagnosticsAST(false, LinksToSkip)->PrepareCycleChecking(InPin);
}

bool URigVMGraph::CanLink(URigVMPin* InSourcePin, URigVMPin* InTargetPin, FString* OutFailureReason, const FRigVMByteCode* InByteCode)
{
	if (!URigVMPin::CanLink(InSourcePin, InTargetPin, OutFailureReason, InByteCode))
	{
		return false;
	}
	return GetDiagnosticsAST()->CanLink(InSourcePin, InTargetPin, OutFailureReason);
}

