// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowConnection.h"

#include "Dataflow/DataflowNodeParameters.h"
#include "Dataflow/DataflowNode.h"

namespace Dataflow
{
	FConnection::FConnection(FPin::EDirection InDirection, FName InType, FName InName, FNode* InOwningNode, FGuid InGuid)
		: Direction(InDirection)
		, Type(InType)
		, Name(InName)
		, Guid(InGuid)
		, OwningNode(InOwningNode)
	{}

	void FConnection::BindInput(FNode* InNode, FConnection* That) 
	{ InNode->AddInput(That); }
	void FConnection::BindOutput(FNode* InNode, FConnection* That) 
	{ InNode->AddOutput(That); }

}

