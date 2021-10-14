// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNode_GetResource.h"

#include "OptimusResourceDescription.h"
#include "OptimusNodePin.h"

void UOptimusNode_GetResource::ConstructNode()
{
	UOptimusResourceDescription *Res = GetResourceDescription();
	if (Res)
	{
		// FIXME: Define context.
		AddPinDirect(
			Res->ResourceName,
			EOptimusNodePinDirection::Output,
			FOptimusNodePinStorageConfig({Optimus::DomainName::Vertex}),
			Res->DataType);
	}
}
