// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMStructNode.h"

UScriptStruct* URigVMStructNode::GetScriptStruct() const
{
	return ScriptStruct;
}

FName URigVMStructNode::GetMethodName() const
{
	return MethodName;
}
