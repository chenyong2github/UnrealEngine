// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNode_SetResource.h"

#include "OptimusNodePin.h"
#include "OptimusResourceDescription.h"


void UOptimusNode_SetResource::ConstructNode()
{
	UOptimusResourceDescription* Res = GetResourceDescription();
	if (Res)
	{
		AddPinDirect(
		    Res->ResourceName,
		    EOptimusNodePinDirection::Input,
			FOptimusNodePinStorageConfig({Optimus::DomainName::Vertex}),
		    Res->DataType);
	}
}
