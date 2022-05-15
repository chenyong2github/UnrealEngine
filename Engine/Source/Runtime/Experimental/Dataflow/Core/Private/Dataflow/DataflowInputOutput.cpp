// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowInputOutput.h"

#include "Dataflow/DataflowNodeParameters.h"
#include "Dataflow/DataflowNode.h"

namespace Dataflow
{
	// Base Types
	EVAL_GRAPH_CONNECTION_TYPE(bool, Bool)
	EVAL_GRAPH_CONNECTION_TYPE(char, Char)
	EVAL_GRAPH_CONNECTION_TYPE(int, Integer)	
	EVAL_GRAPH_CONNECTION_TYPE(uint8, UInt8)
	EVAL_GRAPH_CONNECTION_TYPE(float, Float)
	EVAL_GRAPH_CONNECTION_TYPE(double, Double)
}

