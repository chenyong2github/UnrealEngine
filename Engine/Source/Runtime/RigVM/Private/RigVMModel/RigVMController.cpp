// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/RigVMController.h"
#include "RigVMCore/RigVMRegistry.h"
#include "RigVMModule.h"

URigVMController::~URigVMController()
{
	SetGraph(nullptr);
}

URigVMGraph* URigVMController::GetGraph() const
{
	return Graph;
}

void URigVMController::SetGraph(URigVMGraph* InGraph)
{
	if (Graph)
	{
		Graph->OnModified().RemoveAll(this);
	}

	Graph = InGraph;

	if (Graph)
	{
		Graph->OnModified().AddUObject(this, &URigVMController::HandleModifiedEvent);
	}

	ModifiedEvent.Broadcast(ERigVMGraphNotifType::GraphChanged, Graph, nullptr);
}

FRigVMGraphModifiedEvent& URigVMController::OnModified()
{
	return ModifiedEvent;
}

void URigVMController::Notify(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject)
{
	if (Graph)
	{
		Graph->Notify(InNotifType, InGraph, InSubject);
	}
}

void URigVMController::HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject)
{
	ModifiedEvent.Broadcast(InNotifType, InGraph, InSubject);
}


URigVMStructNode* URigVMController::AddStructNode(UScriptStruct* InScriptStruct, const FName& InMethodName, const FVector2D& InPosition, bool bUndo)
{
	if (Graph == nullptr)
	{
		return nullptr;
	}

	if (InScriptStruct == nullptr)
	{
		return nullptr;
	}
	if (InMethodName == NAME_None)
	{
		return nullptr;
	}

	FString FunctionName = FString::Printf(TEXT("F%s::%s"), *InScriptStruct->GetName(), *InMethodName.ToString());
	FRigVMFunctionPtr Function = FRigVMRegistry::Get().Find(*FunctionName);
	if (Function == nullptr)
	{
		// todo: log error
		return nullptr;
	}

	FString NamePrefix = InScriptStruct->GetName();
	int32 NameSuffix = 0;
	FString Name = NamePrefix;

	while (!Graph->IsNameAvailable(Name))
	{
		Name = FString::Printf(TEXT("%s_%d"), *NamePrefix, ++NameSuffix);
	}

	URigVMStructNode* Node = NewObject<URigVMStructNode>(Graph, *Name);
	Node->ScriptStruct = InScriptStruct;
	Node->MethodName = InMethodName;
	Node->Position = InPosition;

	Graph->Nodes.Add(Node);
	Graph->MarkPackageDirty();

	// todo: undo redo

	Notify(ERigVMGraphNotifType::NodeAdded, Graph, Node);

	return Node;
}

bool URigVMController::RemoveNode(URigVMNode* InNode, bool bUndo)
{
	if (!IsValidNodeForGraph(InNode))
	{
		return false;
	}

	// todo: break all links
	// todo: undo redo

	SelectNode(InNode, false, bUndo);

	Graph->Nodes.Remove(InNode);
	Graph->MarkPackageDirty();

	Notify(ERigVMGraphNotifType::NodeRemoved, Graph, InNode);

	return true;
}

bool URigVMController::RemoveNode(const FName& InNodeName, bool bUndo)
{
	if (Graph == nullptr)
	{
		return false;
	}
	return RemoveNode(Graph->FindNode(InNodeName), bUndo);
}

bool URigVMController::SelectNode(URigVMNode* InNode, bool bSelect, bool bUndo)
{
	if (!IsValidNodeForGraph(InNode))
	{
		return false;
	}

	if (InNode->IsSelected() == bSelect)
	{
		return false;
	}

	if (bSelect)
	{
		Graph->SelectedNodes.Add(InNode->GetFName());
		Notify(ERigVMGraphNotifType::NodeSelected, Graph, InNode);
	}
	else
	{
		Graph->SelectedNodes.Remove(InNode->GetFName());
		Notify(ERigVMGraphNotifType::NodeDeselected, Graph, InNode);
	}

	return true;
}

bool URigVMController::SelectNode(const FName& InNodeName, bool bSelect, bool bUndo)
{
	if (Graph == nullptr)
	{
		return false;
	}
	return SelectNode(Graph->FindNode(InNodeName), bSelect, bUndo);
}

bool URigVMController::ClearNodeSelection(bool bUndo)
{
	if (Graph == nullptr)
	{
		return false;
	}

	TArray<FName> Selection = Graph->SelectedNodes;
	for (const FName& SelectedNode : Selection)
	{
		SelectNode(SelectedNode, false, bUndo);
	}

	return Selection.Num() > 0;
}

bool URigVMController::IsValidNodeForGraph(URigVMNode* InNode)
{
	if (Graph == nullptr)
	{
		return false;
	}

	if (InNode == nullptr)
	{
		return false;
	}

	if (InNode->GetGraph() != Graph)
	{
		return false;
	}

	return true;
}
