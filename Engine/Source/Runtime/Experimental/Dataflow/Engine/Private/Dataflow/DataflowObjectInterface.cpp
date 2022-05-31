// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowObjectInterface.h"

namespace Dataflow
{
	FEngineContext::FEngineContext(UObject* InOwner, UDataflow* InGraph, float InTime, FName InType)
			: FContext(InTime, InType)
			, Owner(InOwner)
			, Graph(InGraph)
		{}
}