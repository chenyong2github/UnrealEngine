// Copyright Epic Games, Inc. All Rights Reserved.

#include "Nodes/OptimusNode_GetResource.h"

#include "OptimusResourceDescription.h"
#include "OptimusNodePin.h"

void UOptimusNode_GetResource::ConstructNode()
{
	if (const UOptimusResourceDescription *Res = GetResourceDescription())
	{
		// FIXME: Define context.
		AddPinDirect(
			Res->ResourceName,
			EOptimusNodePinDirection::Output,
			FOptimusNodePinStorageConfig({Optimus::DomainName::Vertex}),
			Res->DataType);
	}
}
