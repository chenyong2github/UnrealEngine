// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowTerminalNode.h"

#include "ChaosLog.h"
#include "Dataflow/DataflowInputOutput.h"
#include "Dataflow/DataflowArchive.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DataflowTerminalNode)

bool FDataflowTerminalNode::ValidateConnections()
{
	if (ensureMsgf(NumOutputs() == 0, TEXT("Error: Terminal nodes can not have outputs.")))
	{
		return Super::ValidateConnections();
	}
	return false;
}
