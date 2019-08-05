// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/RigVMNode.h"
#include "RigVMModel/RigVMGraph.h"

const TArray<URigVMPin*>& URigVMNode::GetPins() const
{
	return Pins;
}

URigVMGraph* URigVMNode::GetGraph() const
{
	return Cast<URigVMGraph>(GetOuter());
}

FVector2D URigVMNode::GetPosition() const
{
	return Position;
}

bool URigVMNode::IsSelected() const
{
	URigVMGraph* Graph = GetGraph();
	if (Graph)
	{
		return Graph->IsNodeSelected(GetFName());
	}
	return false;
}
