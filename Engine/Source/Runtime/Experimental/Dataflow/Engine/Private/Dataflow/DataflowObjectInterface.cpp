// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowObjectInterface.h"

namespace Dataflow
{
	FEngineContext::FEngineContext(UObject* InOwner, UDataflow* InGraph, float InTime, FString InType)
			: FContext(InTime, StaticType().Append(InType) )
			, Owner(InOwner)
			, Graph(InGraph)
		{}
}