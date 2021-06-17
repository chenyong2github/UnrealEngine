// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMFunctionReturnNode.h"
#include "RigVMModel/RigVMFunctionLibrary.h"

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
