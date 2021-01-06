// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMCollapseNode.h"
#include "RigVMModel/RigVMFunctionLibrary.h"

FString URigVMCollapseNode::GetNodeTitle() const
{
	// todo
	return URigVMNode::GetNodeTitle();
}

FText URigVMCollapseNode::GetToolTipText() const
{
	// todo
	return URigVMNode::GetToolTipText();
}

URigVMFunctionLibrary* URigVMCollapseNode::GetLibrary() const
{
	return Cast<URigVMFunctionLibrary>(GetOuter());
}

FString URigVMCollapseNode::GetEditorSubGraphName() const
{
	return FString::Printf(TEXT("%s_SubGraph"), *GetName());
}