// Copyright Epic Games, Inc. All Rights Reserved.

#include "Nodes/OptimusNode_SetResource.h"

#include "OptimusNodePin.h"
#include "OptimusResourceDescription.h"


void UOptimusNode_SetResource::ConstructNode()
{
	if (const UOptimusResourceDescription* Res = GetResourceDescription())
	{
		AddPinDirect(
		    Res->ResourceName,
		    EOptimusNodePinDirection::Input,
			FOptimusNodePinStorageConfig({Optimus::DomainName::Vertex}),
		    Res->DataType);
	}
}
