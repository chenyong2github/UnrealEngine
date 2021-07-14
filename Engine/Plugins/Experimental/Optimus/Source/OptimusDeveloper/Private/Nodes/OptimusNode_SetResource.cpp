// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNode_SetResource.h"

#include "OptimusNodePin.h"
#include "OptimusResourceDescription.h"


void UOptimusNode_SetResource::CreatePins()
{
	UOptimusResourceDescription* Res = GetResourceDescription();
	if (Res)
	{
		AddPin(
		    Res->ResourceName,
		    EOptimusNodePinDirection::Input,
		    FOptimusNodePinStorageConfig(1, TEXT("Vertex")),
		    Res->DataType);
	}
}
