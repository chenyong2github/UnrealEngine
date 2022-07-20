// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "Dataflow/DataflowGraph.h"
#include "Dataflow/DataflowNodeParameters.h"
#include "GeometryCollection/ManagedArrayCollection.h"

class UDataflow;
class UObject;

namespace Dataflow
{

	class DATAFLOWENGINE_API FEngineContext : public FContext
	{
	public:
		FEngineContext(UObject* Owner, UDataflow* InGraph, float InTime, FString InType = "");
		static FString StaticType() { return "FEngineContext"; }

		UObject* Owner = nullptr;
		UDataflow* Graph = nullptr;
	};
}