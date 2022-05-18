// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowObjectInterface.h"

namespace Dataflow
{
	FEngineContext::FEngineContext(UDataflow* InGraph, float InTime)
			: FContext(InTime)
			, Graph(InGraph) 
		{}
}