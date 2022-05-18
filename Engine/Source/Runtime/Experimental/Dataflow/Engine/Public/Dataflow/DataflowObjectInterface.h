// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Dataflow/DataflowNodeParameters.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "Dataflow/DataflowGraph.h"


namespace Dataflow
{

	class DATAFLOWENGINE_API FEngineContext : public FContext
	{
	public:
		FEngineContext(UDataflow* InGraph, float InTime);
		UDataflow* Graph = nullptr;
	};
}