// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMFunctionReferenceNode.h"
#include "RigVMModel/RigVMFunctionLibrary.h"

FString URigVMFunctionReferenceNode::GetNodeTitle() const
{
	if (URigVMLibraryNode* ReferencedNode = GetReferencedNode())
	{
		return ReferencedNode->GetNodeTitle();
	}
	return Super::GetNodeTitle();
}

FLinearColor URigVMFunctionReferenceNode::GetNodeColor() const
{
	if (URigVMLibraryNode* ReferencedNode = GetReferencedNode())
	{
		return ReferencedNode->GetNodeColor();
	}
	return Super::GetNodeColor();
}

FText URigVMFunctionReferenceNode::GetToolTipText() const
{
	if (URigVMLibraryNode* ReferencedNode = GetReferencedNode())
	{
		return ReferencedNode->GetToolTipText();
	}
	return Super::GetToolTipText();
}

URigVMFunctionLibrary* URigVMFunctionReferenceNode::GetLibrary() const
{
	if(URigVMLibraryNode* ReferencedNode = GetReferencedNode())
	{
		return ReferencedNode->GetLibrary();
	}
	return nullptr;
}

URigVMGraph* URigVMFunctionReferenceNode::GetContainedGraph() const
{
	if (URigVMLibraryNode* ReferencedNode = GetReferencedNode())
	{
		return ReferencedNode->GetContainedGraph();
	}
	return nullptr;
}

URigVMLibraryNode* URigVMFunctionReferenceNode::GetReferencedNode() const
{
	if (!ReferencedNodePtr.IsValid())
	{
		ReferencedNodePtr.LoadSynchronous();
	}
	return ReferencedNodePtr.Get();
}

void URigVMFunctionReferenceNode::SetReferencedNode(URigVMLibraryNode* InReferenceNode)
{
	ReferencedNodePtr = InReferenceNode;
}