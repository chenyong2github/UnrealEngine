// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowConnection.h"

#include "Dataflow/DataflowNodeParameters.h"
#include "Dataflow/DataflowNode.h"

FDataflowConnection::FDataflowConnection(Dataflow::FPin::EDirection InDirection, FName InType, FName InName, FDataflowNode* InOwningNode, FProperty* InProperty, FGuid InGuid)
	: Direction(InDirection)
	, Type(InType)
	, Name(InName)
	, OwningNode(InOwningNode)
	, Property(InProperty)
	, Guid(InGuid)
{}

// @todo(dataflow) check that the return types are all consistent
uint32 FDataflowConnection::GetOffset() const
{
	if (ensure(Property != nullptr))
	{
		return Property->GetOffset_ForInternal();
	}
	return (uint32)INDEX_NONE;
}


