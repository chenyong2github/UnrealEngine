// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMFunctionEntryNode.h"
#include "RigVMModel/RigVMFunctionLibrary.h"

FLinearColor URigVMFunctionEntryNode::GetNodeColor() const
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

bool URigVMFunctionEntryNode::IsDefinedAsVarying() const
{ 
	// todo
	return true; 
}
