// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMController.h"
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
	// for now we don't support encapsulation
	// so we don't walk recursively
	return FindNodeByName(FName(*InNodePath));
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

FRigVMGraphModifiedEvent& URigVMGraph::OnModified()
{
	return ModifiedEvent;
}

void URigVMGraph::Notify(ERigVMGraphNotifType InNotifType, UObject* InSubject)
{
	ModifiedEvent.Broadcast(InNotifType, this, InSubject);
}

TSharedPtr<FRigVMParserAST> URigVMGraph::GetDiagnosticsAST(bool bForceRefresh)
{
	if (DiagnosticsAST == nullptr || bForceRefresh)
	{
		FRigVMParserASTSettings Settings = FRigVMParserASTSettings::Fast();
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
	GetDiagnosticsAST()->PrepareCycleChecking(InPin);
}

bool URigVMGraph::CanLink(URigVMPin* InSourcePin, URigVMPin* InTargetPin, FString* OutFailureReason)
{
	if (!URigVMPin::CanLink(InSourcePin, InTargetPin, OutFailureReason))
	{
		return false;
	}
	return GetDiagnosticsAST()->CanLink(InSourcePin, InTargetPin, OutFailureReason);
}

