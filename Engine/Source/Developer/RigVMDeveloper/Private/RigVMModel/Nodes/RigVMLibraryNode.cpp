// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMLibraryNode.h"
#include "RigVMModel/Nodes/RigVMFunctionEntryNode.h"
#include "RigVMModel/Nodes/RigVMFunctionReturnNode.h"
#include "RigVMModel/RigVMGraph.h"

bool URigVMLibraryNode::IsDefinedAsConstant() const
{
	return !bDefinedAsVarying;
}

bool URigVMLibraryNode::IsDefinedAsVarying() const
{
	return bDefinedAsVarying;
}

TArray<URigVMNode*> URigVMLibraryNode::GetContainedNodes() const
{
	if(URigVMGraph* Graph = GetContainedGraph())
	{
		return Graph->GetNodes();
	}
	return TArray<URigVMNode*>();
}

TArray<URigVMLink*> URigVMLibraryNode::GetContainedLinks() const
{
	if (URigVMGraph* Graph = GetContainedGraph())
	{
		return Graph->GetLinks();
	}
	return TArray<URigVMLink*>();

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
