// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMStructNode.h"

const TArray<URigVMNode*>& URigVMGraph::GetNodes() const
{
	return Nodes;
}

URigVMNode* URigVMGraph::FindNode(const FName& InNodeName)
{
	for (URigVMNode* Node : Nodes)
	{
		if (Node->GetFName() == InNodeName)
		{
			return Node;
		}
	}
	return nullptr;
}

bool URigVMGraph::IsNodeSelected(const FName& InNodeName) const
{
	return SelectedNodes.Contains(InNodeName);
}

FRigVMGraphModifiedEvent& URigVMGraph::OnModified()
{
	return ModifiedEvent;
}

void URigVMGraph::Notify(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject)
{
	ModifiedEvent.Broadcast(InNotifType, InGraph, InSubject);
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
