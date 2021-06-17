// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMCollapseNode.h"
#include "RigVMModel/RigVMFunctionLibrary.h"

URigVMCollapseNode::URigVMCollapseNode()
	: URigVMLibraryNode()
{
	NodeColor = FLinearColor::White;
}

FText URigVMCollapseNode::GetToolTipText() const
{
	const FString ToolTipString = GetNodeDescription();
	if(!ToolTipString.IsEmpty())
	{
		return FText::FromString(ToolTipString);
	}
	return Super::GetToolTipText();
}

URigVMFunctionLibrary* URigVMCollapseNode::GetLibrary() const
{
	return Cast<URigVMFunctionLibrary>(GetOuter());
}

FString URigVMCollapseNode::GetEditorSubGraphName() const
{
	return FString::Printf(TEXT("%s_SubGraph"), *GetName());
}