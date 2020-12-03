// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMLibraryNode.h"
#include "RigVMModel/Nodes/RigVMFunctionEntryNode.h"
#include "RigVMModel/Nodes/RigVMFunctionReturnNode.h"
#include "RigVMModel/RigVMGraph.h"

const TArray<URigVMNode*> URigVMLibraryNode::EmptyNodes;
const TArray<URigVMLink*> URigVMLibraryNode::EmptyLinks;

bool URigVMLibraryNode::IsDefinedAsConstant() const
{
	return !IsDefinedAsVarying();
}

bool URigVMLibraryNode::IsDefinedAsVarying() const
{
	if (URigVMGraph* Graph = GetContainedGraph())
	{
		const TArray<URigVMNode*>& Nodes = Graph->GetNodes();
		for(URigVMNode* Node : Nodes)
		{
			if (Node->IsDefinedAsVarying())
			{
				return true;
			}
		}
	}
	return false;
}

const TArray<URigVMNode*>& URigVMLibraryNode::GetContainedNodes() const
{
	if(URigVMGraph* Graph = GetContainedGraph())
	{
		return Graph->GetNodes();
	}
	return EmptyNodes;
}

const TArray<URigVMLink*>& URigVMLibraryNode::GetContainedLinks() const
{
	if (URigVMGraph* Graph = GetContainedGraph())
	{
		return Graph->GetLinks();
	}
	return EmptyLinks;

}

URigVMFunctionEntryNode* URigVMLibraryNode::GetEntryNode() const
{
	TArray<URigVMNode*> ContainedNodes = GetContainedNodes();
	for (URigVMNode* ContainedNode : ContainedNodes)
	{
		if (URigVMFunctionEntryNode* EntryNode = Cast<URigVMFunctionEntryNode>(ContainedNode))
		{
			return EntryNode;
		}
	}
	return nullptr;
}

URigVMFunctionReturnNode* URigVMLibraryNode::GetReturnNode() const
{
	TArray<URigVMNode*> ContainedNodes = GetContainedNodes();
	for (URigVMNode* ContainedNode : ContainedNodes)
	{
		if (URigVMFunctionReturnNode* ReturnNode = Cast<URigVMFunctionReturnNode>(ContainedNode))
		{
			return ReturnNode;
		}
	}
	return nullptr;
}
