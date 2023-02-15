// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMFunctionReturnNode.h"
#include "RigVMModel/RigVMFunctionLibrary.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigVMFunctionReturnNode)

FLinearColor URigVMFunctionReturnNode::GetNodeColor() const
{
	if(URigVMGraph* RootGraph = GetRootGraph())
	{
		if(RootGraph->IsA<URigVMFunctionLibrary>())
		{
			return FLinearColor(FColor::FromHex("CB00FFFF"));
		}
	}
	return FLinearColor(FColor::FromHex("005DFFFF"));
}

bool URigVMFunctionReturnNode::IsDefinedAsVarying() const
{ 
	// todo
	return true; 
}

FString URigVMFunctionReturnNode::GetNodeTitle() const
{
	return TEXT("Return");
}

FText URigVMFunctionReturnNode::GetToolTipText() const
{
	return FText::FromName(GetGraph()->GetOuter()->GetFName());
}

FText URigVMFunctionReturnNode::GetToolTipTextForPin(const URigVMPin* InPin) const
{
	return Super::GetToolTipTextForPin(InPin);
}
