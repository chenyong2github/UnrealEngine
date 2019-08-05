// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/RigVMPin.h"
#include "RigVMModel/RigVMNode.h"

URigVMPin::URigVMPin()
	: Direction(ERigVMPinDirection::Invalid)
	, bIsConstant(false)
	, ArrayIndex(INDEX_NONE)
	, CPPType(FString())
{
}

FString URigVMPin::GetPinPath() const
{
	// todo
	return FString();
}

ERigVMPinDirection URigVMPin::GetDirection() const
{
	return Direction;
}

bool URigVMPin::IsConstant() const
{
	return bIsConstant;
}

int32 URigVMPin::IsArray() const
{
	return ArrayIndex != INDEX_NONE;
}

int32 URigVMPin::GetArrayIndex() const
{
	return ArrayIndex;
}

FString URigVMPin::GetCPPType() const
{
	return CPPType;
}

URigVMPin* URigVMPin::GetParentPin() const
{
	return Cast<URigVMPin>(GetOuter());
}

const TArray<URigVMPin*>& URigVMPin::GetSubPins() const
{
	return SubPins;
}

const TArray<URigVMPin*>& URigVMPin::GetConnectedPins() const
{
	return ConnectedPins;
}

URigVMNode* URigVMPin::GetNode() const
{
	URigVMNode* Node = Cast<URigVMNode>(GetOuter());
	if(Node)
	{
		return Node;
	}

	URigVMPin* ParentPin = GetParentPin();
	if(ParentPin)
	{
		return ParentPin->GetNode();
	}

	return nullptr;
}

URigVMGraph* URigVMPin::GetGraph() const
{
	URigVMNode* Node = GetNode();
	if(Node)
	{
		return Node->GetGraph();
	}

	return nullptr;
}
