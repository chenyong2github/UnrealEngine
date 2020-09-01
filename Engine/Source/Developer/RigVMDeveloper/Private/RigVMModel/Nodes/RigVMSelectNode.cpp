// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMSelectNode.h"

const FString URigVMSelectNode::SelectName = TEXT("Select");
const FString URigVMSelectNode::IndexName = TEXT("Index");
const FString URigVMSelectNode::ValueName = TEXT("Values");
const FString URigVMSelectNode::ResultName = TEXT("Result");

bool URigVMSelectNode::AllowsLinksOn(const URigVMPin* InPin) const
{
	if(InPin->GetRootPin() == InPin)
	{
		if(InPin->GetName() == ValueName)
		{
			return false;
		}
	}

	return true;
}
